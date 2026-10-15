#include "libaccel.h"
#include "mnist_model.h"

#define LINUX_SYSTEM_CALL_NUMBER_WRITE 64
#define LINUX_SYSTEM_CALL_NUMBER_EXIT  93

static inline long system_call_write_standard_output(int file_descriptor, const void *buffer_pointer, size_t byte_count) {
    register long system_call_register_a7 asm("a7") = LINUX_SYSTEM_CALL_NUMBER_WRITE;
    register long system_call_register_a0 asm("a0") = file_descriptor;
    register long system_call_register_a1 asm("a1") = (long)buffer_pointer;
    register long system_call_register_a2 asm("a2") = byte_count;
    asm volatile("ecall" 
                 : "+r"(system_call_register_a0) 
                 : "r"(system_call_register_a1), "r"(system_call_register_a2), "r"(system_call_register_a7) 
                 : "memory");
    return system_call_register_a0;
}

static inline void system_call_exit_process(int exit_status_code) {
    register long system_call_register_a7 asm("a7") = LINUX_SYSTEM_CALL_NUMBER_EXIT;
    register long system_call_register_a0 asm("a0") = exit_status_code;
    asm volatile("ecall" 
                 : "+r"(system_call_register_a0) 
                 : "r"(system_call_register_a7) 
                 : "memory");
    while (1);
}

static size_t calculate_string_length(const char *string_pointer) {
    size_t string_length_counter = 0;
    while (string_pointer[string_length_counter]) {
        string_length_counter++;
    }
    return string_length_counter;
}

static void print_string_to_standard_output(const char *string_pointer) {
    system_call_write_standard_output(1, string_pointer, calculate_string_length(string_pointer));
}

static void print_decimal_integer_to_standard_output(int32_t integer_value) {
    char character_buffer[16];
    int character_index = 0;
    int is_negative_number = 0;
    if (integer_value < 0) {
        is_negative_number = 1;
        integer_value = -integer_value;
    }
    if (integer_value == 0) {
        character_buffer[character_index++] = '0';
    } else {
        while (integer_value > 0) {
            character_buffer[character_index++] = '0' + (integer_value % 10);
            integer_value /= 10;
        }
        if (is_negative_number) {
            character_buffer[character_index++] = '-';
        }
    }
    char reversed_character_buffer[16];
    for (int reverse_index = 0; reverse_index < character_index; reverse_index++) {
        reversed_character_buffer[reverse_index] = character_buffer[character_index - 1 - reverse_index];
    }
    system_call_write_standard_output(1, reversed_character_buffer, character_index);
}

static void render_ascii_digit_image_to_console(const int8_t *image_pixel_array) {
    print_string_to_standard_output("\n");
    for (int row_index = 0; row_index < 28; row_index++) {
        print_string_to_standard_output("  ");
        for (int column_index = 0; column_index < 28; column_index++) {
            int8_t pixel_grayscale_value = image_pixel_array[row_index * 28 + column_index];
            if (pixel_grayscale_value > 80) {
                print_string_to_standard_output("# ");
            } else if (pixel_grayscale_value > 25) {
                print_string_to_standard_output(". ");
            } else {
                print_string_to_standard_output("  ");
            }
        }
        print_string_to_standard_output("\n");
    }
    print_string_to_standard_output("\n");
}

static int find_maximum_score_class_index_10(const int8_t *output_logits_array) {
    int maximum_score_class_index = 0;
    int8_t maximum_logit_score = output_logits_array[0];
    for (int class_candidate_index = 1; class_candidate_index < 10; class_candidate_index++) {
        if (output_logits_array[class_candidate_index] > maximum_logit_score) {
            maximum_logit_score = output_logits_array[class_candidate_index];
            maximum_score_class_index = class_candidate_index;
        }
    }
    return maximum_score_class_index;
}

void _start(void) {
    accelerator_device_handle_t accelerator_handle;
    int initialization_return_code = accelerator_hardware_initialize(&accelerator_handle, ACCELERATOR_MODE_DIRECT_MEMORY_ACCESS);
    if (initialization_return_code != 0) {
        print_string_to_standard_output("mnist: accel init failed\n");
        system_call_exit_process(1);
    }
    print_string_to_standard_output("mnist: dma init ok\n");

    uint32_t physical_address_weight_matrix_1 = accelerator_preload_weight_matrix_to_memory(&accelerator_handle, mnist_w1, sizeof(mnist_w1), 0);
    uint32_t physical_address_weight_matrix_2 = accelerator_preload_weight_matrix_to_memory(&accelerator_handle, mnist_w2, sizeof(mnist_w2), 51200);
    print_string_to_standard_output("mnist: weights preloaded (w1 @ 0x48000000, w2 @ 0x4800c800)\n\n");

    const int8_t *test_images_array[3] = {
        mnist_test_img_0,
        mnist_test_img_1,
        mnist_test_img_2
    };

    static int8_t __attribute__((aligned(4))) hidden_layer_1_activations_cpu[MNIST_H1_DIM];
    static int8_t __attribute__((aligned(4))) output_logits_cpu[MNIST_OUT_DIM];

    static int8_t __attribute__((aligned(4))) hidden_layer_1_activations_programmed_io[MNIST_H1_DIM];
    static int8_t __attribute__((aligned(4))) output_logits_programmed_io[MNIST_OUT_DIM];

    static int8_t __attribute__((aligned(4))) hidden_layer_1_activations_direct_memory_access[MNIST_H1_DIM];
    static int8_t __attribute__((aligned(4))) output_logits_direct_memory_access[MNIST_OUT_DIM];

    #define BENCHMARK_INFERENCE_REPETITIONS_COUNT 50

    for (int test_image_index = 0; test_image_index < 3; test_image_index++) {
        const int8_t *current_test_image = test_images_array[test_image_index];
        int expected_ground_truth_digit = mnist_test_labels[test_image_index];

        print_string_to_standard_output("--- image ");
        print_decimal_integer_to_standard_output(test_image_index + 1);
        print_string_to_standard_output(" (expected: ");
        print_decimal_integer_to_standard_output(expected_ground_truth_digit);
        print_string_to_standard_output(") ---\n");

        render_ascii_digit_image_to_console(current_test_image);

        // CPU
        uint32_t timestamp_cpu_start_microseconds = accelerator_get_current_time_microseconds();
        for (int repetition_index = 0; repetition_index < BENCHMARK_INFERENCE_REPETITIONS_COUNT; repetition_index++) {
            cpu_compute_dense_neural_layer(current_test_image, mnist_w1, mnist_b1, hidden_layer_1_activations_cpu, MNIST_IN_DIM, MNIST_H1_DIM, MNIST_SHIFT_1, 1);
            cpu_compute_dense_neural_layer(hidden_layer_1_activations_cpu, mnist_w2, mnist_b2, output_logits_cpu, MNIST_H1_DIM, MNIST_OUT_DIM, 0, 0);
        }
        uint32_t timestamp_cpu_end_microseconds = accelerator_get_current_time_microseconds();
        uint32_t average_duration_cpu_microseconds = (timestamp_cpu_end_microseconds >= timestamp_cpu_start_microseconds) 
            ? ((timestamp_cpu_end_microseconds - timestamp_cpu_start_microseconds) / BENCHMARK_INFERENCE_REPETITIONS_COUNT) 
            : 1;
        int predicted_digit_cpu = find_maximum_score_class_index_10(output_logits_cpu);

        // PIO
        accelerator_handle.execution_mode = ACCELERATOR_MODE_PROGRAMMED_IO;
        uint32_t timestamp_programmed_io_start_microseconds = accelerator_get_current_time_microseconds();
        for (int repetition_index = 0; repetition_index < BENCHMARK_INFERENCE_REPETITIONS_COUNT; repetition_index++) {
            accelerator_compute_dense_neural_layer(&accelerator_handle, current_test_image, mnist_w1, mnist_b1, hidden_layer_1_activations_programmed_io, MNIST_IN_DIM, MNIST_H1_DIM, MNIST_SHIFT_1, 1);
            accelerator_compute_dense_neural_layer(&accelerator_handle, hidden_layer_1_activations_programmed_io, mnist_w2, mnist_b2, output_logits_programmed_io, MNIST_H1_DIM, MNIST_OUT_DIM, 0, 0);
        }
        uint32_t timestamp_programmed_io_end_microseconds = accelerator_get_current_time_microseconds();
        uint32_t average_duration_programmed_io_microseconds = (timestamp_programmed_io_end_microseconds >= timestamp_programmed_io_start_microseconds) 
            ? ((timestamp_programmed_io_end_microseconds - timestamp_programmed_io_start_microseconds) / BENCHMARK_INFERENCE_REPETITIONS_COUNT) 
            : 1;
        int predicted_digit_programmed_io = find_maximum_score_class_index_10(output_logits_programmed_io);

        // DMA Fast Zero-Copy
        accelerator_handle.execution_mode = ACCELERATOR_MODE_DIRECT_MEMORY_ACCESS;
        uint32_t timestamp_direct_memory_access_start_microseconds = accelerator_get_current_time_microseconds();
        for (int repetition_index = 0; repetition_index < BENCHMARK_INFERENCE_REPETITIONS_COUNT; repetition_index++) {
            accelerator_compute_dense_layer_zero_copy(&accelerator_handle, current_test_image, physical_address_weight_matrix_1, mnist_b1, hidden_layer_1_activations_direct_memory_access, MNIST_IN_DIM, MNIST_H1_DIM, MNIST_SHIFT_1, 1);
            accelerator_compute_dense_layer_zero_copy(&accelerator_handle, hidden_layer_1_activations_direct_memory_access, physical_address_weight_matrix_2, mnist_b2, output_logits_direct_memory_access, MNIST_H1_DIM, MNIST_OUT_DIM, 0, 0);
        }
        uint32_t timestamp_direct_memory_access_end_microseconds = accelerator_get_current_time_microseconds();
        uint32_t average_duration_direct_memory_access_microseconds = (timestamp_direct_memory_access_end_microseconds >= timestamp_direct_memory_access_start_microseconds) 
            ? ((timestamp_direct_memory_access_end_microseconds - timestamp_direct_memory_access_start_microseconds) / BENCHMARK_INFERENCE_REPETITIONS_COUNT) 
            : 1;
        int predicted_digit_direct_memory_access = find_maximum_score_class_index_10(output_logits_direct_memory_access);

        // Results
        print_string_to_standard_output("cpu : pred=");
        print_decimal_integer_to_standard_output(predicted_digit_cpu);
        print_string_to_standard_output(" | ");
        print_decimal_integer_to_standard_output(average_duration_cpu_microseconds);
        print_string_to_standard_output(" us\n");

        print_string_to_standard_output("pio : pred=");
        print_decimal_integer_to_standard_output(predicted_digit_programmed_io);
        print_string_to_standard_output(" | ");
        print_decimal_integer_to_standard_output(average_duration_programmed_io_microseconds);
        print_string_to_standard_output(" us (");
        if (average_duration_programmed_io_microseconds > 0) {
            uint32_t speedup_ratio_programmed_io_vs_cpu_x10 = (average_duration_cpu_microseconds * 10) / average_duration_programmed_io_microseconds;
            print_decimal_integer_to_standard_output(speedup_ratio_programmed_io_vs_cpu_x10 / 10);
            print_string_to_standard_output(".");
            print_decimal_integer_to_standard_output(speedup_ratio_programmed_io_vs_cpu_x10 % 10);
            print_string_to_standard_output("x");
        }
        print_string_to_standard_output(")\n");

        print_string_to_standard_output("dma : pred=");
        print_decimal_integer_to_standard_output(predicted_digit_direct_memory_access);
        print_string_to_standard_output(" | ");
        print_decimal_integer_to_standard_output(average_duration_direct_memory_access_microseconds);
        print_string_to_standard_output(" us (");
        if (average_duration_direct_memory_access_microseconds > 0) {
            uint32_t speedup_ratio_direct_memory_access_vs_cpu_x10 = (average_duration_cpu_microseconds * 10) / average_duration_direct_memory_access_microseconds;
            print_decimal_integer_to_standard_output(speedup_ratio_direct_memory_access_vs_cpu_x10 / 10);
            print_string_to_standard_output(".");
            print_decimal_integer_to_standard_output(speedup_ratio_direct_memory_access_vs_cpu_x10 % 10);
            print_string_to_standard_output("x");
        }
        print_string_to_standard_output(")\n");

        if (predicted_digit_cpu == expected_ground_truth_digit && 
            predicted_digit_programmed_io == expected_ground_truth_digit && 
            predicted_digit_direct_memory_access == expected_ground_truth_digit) {
            print_string_to_standard_output("status: match\n\n");
        } else if (predicted_digit_cpu == predicted_digit_programmed_io && 
                   predicted_digit_programmed_io == predicted_digit_direct_memory_access) {
            print_string_to_standard_output("status: match (diverges from label)\n\n");
        } else {
            print_string_to_standard_output("status: mismatch\n\n");
        }
    }

    accelerator_hardware_release(&accelerator_handle);
    system_call_exit_process(0);
}