#include "libaccel.h"

#define LINUX_SYSTEM_CALL_NUMBER_WRITE            64
#define LINUX_SYSTEM_CALL_NUMBER_EXIT             93
#define LINUX_SYSTEM_CALL_NUMBER_MMAP2            222
#define LINUX_SYSTEM_CALL_NUMBER_OPENAT           56
#define LINUX_SYSTEM_CALL_NUMBER_CLOSE            57
#define LINUX_SYSTEM_CALL_NUMBER_CLOCK_GETTIME64  403

#define FILE_OPEN_FLAG_READ_WRITE                 00000002
#define FILE_OPEN_FLAG_SYNCHRONOUS_IO             00010000
#define MEMORY_PROTECTION_READ                    0x1
#define MEMORY_PROTECTION_WRITE                   0x2
#define MEMORY_MAP_SHARED                         0x01
#define FILE_DESCRIPTOR_CURRENT_WORKING_DIRECTORY -100
#define POSIX_CLOCK_MONOTONIC                     1

#define REGISTER_OFFSET_CONTROL                   0
#define REGISTER_OFFSET_STATUS                    1
#define REGISTER_OFFSET_DATA_A                    2
#define REGISTER_OFFSET_DATA_B                    3
#define REGISTER_OFFSET_ACCUMULATOR_OUTPUT        4
#define REGISTER_OFFSET_DMA_SOURCE_ADDRESS_A      5
#define REGISTER_OFFSET_DMA_SOURCE_ADDRESS_B      6
#define REGISTER_OFFSET_DMA_TRANSFER_LENGTH       7

#define CONTROL_BIT_START_PROGRAMMED_IO           0x01
#define CONTROL_BIT_CLEAR_ACCUMULATOR             0x02
#define CONTROL_BIT_START_DIRECT_MEMORY_ACCESS    0x08
#define STATUS_BIT_BUSY_PROGRAMMED_IO             0x01
#define STATUS_BIT_DONE_DIRECT_MEMORY_ACCESS      0x10

#define ACCELERATOR_BASE_PHYSICAL_ADDRESS         0x80002000
#define DMA_BUFFER_A_BASE_PHYSICAL_ADDRESS        0x48000000
#define DMA_TOTAL_BUFFER_SIZE_BYTES               131072
#define DMA_BUFFER_INPUT_OFFSET_BYTES             65536
#define DMA_BUFFER_B_BASE_PHYSICAL_ADDRESS        (DMA_BUFFER_A_BASE_PHYSICAL_ADDRESS + DMA_BUFFER_INPUT_OFFSET_BYTES)

static inline long system_call_openat(int directory_file_descriptor, const char *file_path, int open_flags, int file_mode) {
    register long system_call_register_a7 asm("a7") = LINUX_SYSTEM_CALL_NUMBER_OPENAT;
    register long system_call_register_a0 asm("a0") = directory_file_descriptor;
    register long system_call_register_a1 asm("a1") = (long)file_path;
    register long system_call_register_a2 asm("a2") = open_flags;
    register long system_call_register_a3 asm("a3") = file_mode;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a1), "r"(system_call_register_a2), "r"(system_call_register_a3), "r"(system_call_register_a7) : "memory");
    return system_call_register_a0;
}

static inline void *system_call_mmap2(void *target_address, size_t allocation_length, int memory_protection, int map_flags, int file_descriptor, size_t page_offset) {
    register long system_call_register_a7 asm("a7") = LINUX_SYSTEM_CALL_NUMBER_MMAP2;
    register long system_call_register_a0 asm("a0") = (long)target_address;
    register long system_call_register_a1 asm("a1") = allocation_length;
    register long system_call_register_a2 asm("a2") = memory_protection;
    register long system_call_register_a3 asm("a3") = map_flags;
    register long system_call_register_a4 asm("a4") = file_descriptor;
    register long system_call_register_a5 asm("a5") = page_offset;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a1), "r"(system_call_register_a2), "r"(system_call_register_a3), "r"(system_call_register_a4), "r"(system_call_register_a5), "r"(system_call_register_a7) : "memory");
    return (void *)system_call_register_a0;
}

static inline long system_call_close(int file_descriptor) {
    register long system_call_register_a7 asm("a7") = LINUX_SYSTEM_CALL_NUMBER_CLOSE;
    register long system_call_register_a0 asm("a0") = file_descriptor;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a7) : "memory");
    return system_call_register_a0;
}

struct linux_timespec64_structure {
    uint32_t seconds_lower_word;
    uint32_t seconds_upper_word;
    uint32_t nanoseconds_lower_word;
    uint32_t nanoseconds_upper_word;
};

uint32_t accelerator_get_current_time_microseconds(void) {
    struct linux_timespec64_structure timestamp_structure = {0, 0, 0, 0};
    register long system_call_register_a7 asm("a7") = LINUX_SYSTEM_CALL_NUMBER_CLOCK_GETTIME64;
    register long system_call_register_a0 asm("a0") = POSIX_CLOCK_MONOTONIC;
    register long system_call_register_a1 asm("a1") = (long)&timestamp_structure;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a1), "r"(system_call_register_a7) : "memory");
    return (timestamp_structure.seconds_lower_word * 1000000) + (timestamp_structure.nanoseconds_lower_word / 1000);
}

int accelerator_hardware_initialize(accelerator_device_handle_t *accelerator_handle, accelerator_execution_mode_t execution_mode) {
    const char memory_character_device_path[] = "/dev/mem";
    accelerator_handle->file_descriptor_memory = system_call_openat(
        FILE_DESCRIPTOR_CURRENT_WORKING_DIRECTORY, 
        memory_character_device_path, 
        FILE_OPEN_FLAG_READ_WRITE | FILE_OPEN_FLAG_SYNCHRONOUS_IO, 
        0
    );
    if (accelerator_handle->file_descriptor_memory < 0) return -1;

    accelerator_handle->virtual_control_status_registers = (volatile uint32_t *)system_call_mmap2(
        0, 
        4096, 
        MEMORY_PROTECTION_READ | MEMORY_PROTECTION_WRITE, 
        MEMORY_MAP_SHARED, 
        accelerator_handle->file_descriptor_memory, 
        ACCELERATOR_BASE_PHYSICAL_ADDRESS >> 12
    );
    if ((unsigned long)accelerator_handle->virtual_control_status_registers >= 0xfffff000UL) {
        system_call_close(accelerator_handle->file_descriptor_memory);
        return -2;
    }

    accelerator_handle->dma_buffer_size_bytes = DMA_TOTAL_BUFFER_SIZE_BYTES;
    accelerator_handle->dma_virtual_buffer_address = system_call_mmap2(
        0, 
        accelerator_handle->dma_buffer_size_bytes, 
        MEMORY_PROTECTION_READ | MEMORY_PROTECTION_WRITE, 
        MEMORY_MAP_SHARED, 
        accelerator_handle->file_descriptor_memory, 
        DMA_BUFFER_A_BASE_PHYSICAL_ADDRESS >> 12
    );
    if ((unsigned long)accelerator_handle->dma_virtual_buffer_address >= 0xfffff000UL) {
        system_call_close(accelerator_handle->file_descriptor_memory);
        return -3;
    }

    accelerator_handle->dma_physical_buffer_address_a = DMA_BUFFER_A_BASE_PHYSICAL_ADDRESS;
    accelerator_handle->dma_physical_buffer_address_b = DMA_BUFFER_B_BASE_PHYSICAL_ADDRESS;
    accelerator_handle->execution_mode               = execution_mode;
    return 0;
}

void accelerator_hardware_release(accelerator_device_handle_t *accelerator_handle) {
    if (accelerator_handle->file_descriptor_memory >= 0) {
        system_call_close(accelerator_handle->file_descriptor_memory);
        accelerator_handle->file_descriptor_memory = -1;
    }
}

int32_t cpu_compute_vector_dot_product(const int8_t *vector_operand_a, const int8_t *vector_operand_b, size_t vector_length_elements) {
    int32_t accumulator_sum = 0;
    for (size_t element_index = 0; element_index < vector_length_elements; element_index++) {
        accumulator_sum += (int32_t)vector_operand_a[element_index] * (int32_t)vector_operand_b[element_index];
    }
    return accumulator_sum;
}

int32_t accelerator_compute_vector_dot_product(accelerator_device_handle_t *accelerator_handle, const int8_t *vector_operand_a, const int8_t *vector_operand_b, size_t vector_length_elements) {
    size_t vector_length_words_32bit = vector_length_elements / 4;

    if (accelerator_handle->execution_mode == ACCELERATOR_MODE_PROGRAMMED_IO) {
        const uint32_t *vector_operand_a_words = (const uint32_t *)vector_operand_a;
        const uint32_t *vector_operand_b_words = (const uint32_t *)vector_operand_b;

        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DATA_A] = vector_operand_a_words[0];
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DATA_B] = vector_operand_b_words[0];
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_CONTROL] = (CONTROL_BIT_START_PROGRAMMED_IO | CONTROL_BIT_CLEAR_ACCUMULATOR);

        for (size_t word_index = 1; word_index < vector_length_words_32bit; word_index++) {
            accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DATA_A] = vector_operand_a_words[word_index];
            accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DATA_B] = vector_operand_b_words[word_index];
            accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_CONTROL] = CONTROL_BIT_START_PROGRAMMED_IO;
        }

        while (accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_STATUS] & STATUS_BIT_BUSY_PROGRAMMED_IO);
        return (int32_t)accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_ACCUMULATOR_OUTPUT];
    } else {
        int8_t *dma_buffer_pointer_a = (int8_t *)accelerator_handle->dma_virtual_buffer_address;
        int8_t *dma_buffer_pointer_b = (int8_t *)((uint8_t *)accelerator_handle->dma_virtual_buffer_address + DMA_BUFFER_INPUT_OFFSET_BYTES);

        uint32_t *dma_words_pointer_a = (uint32_t *)dma_buffer_pointer_a;
        uint32_t *dma_words_pointer_b = (uint32_t *)dma_buffer_pointer_b;
        const uint32_t *source_words_a = (const uint32_t *)vector_operand_a;
        const uint32_t *source_words_b = (const uint32_t *)vector_operand_b;

        for (size_t word_index = 0; word_index < vector_length_words_32bit; word_index++) {
            dma_words_pointer_a[word_index] = source_words_a[word_index];
            dma_words_pointer_b[word_index] = source_words_b[word_index];
        }

        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_SOURCE_ADDRESS_A] = accelerator_handle->dma_physical_buffer_address_a;
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_SOURCE_ADDRESS_B] = accelerator_handle->dma_physical_buffer_address_b;
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_TRANSFER_LENGTH]   = (uint32_t)vector_length_words_32bit;
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_CONTROL]              = CONTROL_BIT_START_DIRECT_MEMORY_ACCESS;

        while (!((accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_STATUS] >> 4) & 0x01));
        return (int32_t)accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_ACCUMULATOR_OUTPUT];
    }
}

void cpu_compute_dense_neural_layer(const int8_t *input_activations, 
                                    const int8_t *weight_matrix, 
                                    const int32_t *bias_vector, 
                                    int8_t *output_activations, 
                                    size_t input_dimension, 
                                    size_t output_dimension, 
                                    int requantization_shift_bits, 
                                    int enable_relu_activation) {
    for (size_t neuron_output_index = 0; neuron_output_index < output_dimension; neuron_output_index++) {
        const int8_t *current_weight_row = weight_matrix + (neuron_output_index * input_dimension);
        int32_t accumulator_value = cpu_compute_vector_dot_product(current_weight_row, input_activations, input_dimension);
        if (bias_vector) accumulator_value += bias_vector[neuron_output_index];

        int32_t scaled_activation_value = (requantization_shift_bits > 0) 
            ? ((accumulator_value + (1 << (requantization_shift_bits - 1))) >> requantization_shift_bits) 
            : accumulator_value;
        if (enable_relu_activation && scaled_activation_value < 0) scaled_activation_value = 0;
        if (scaled_activation_value > 127)  scaled_activation_value = 127;
        if (scaled_activation_value < -128) scaled_activation_value = -128;

        output_activations[neuron_output_index] = (int8_t)scaled_activation_value;
    }
}

void accelerator_compute_dense_neural_layer(accelerator_device_handle_t *accelerator_handle, 
                                            const int8_t *input_activations, 
                                            const int8_t *weight_matrix, 
                                            const int32_t *bias_vector, 
                                            int8_t *output_activations, 
                                            size_t input_dimension, 
                                            size_t output_dimension, 
                                            int requantization_shift_bits, 
                                            int enable_relu_activation) {
    size_t words_per_neuron_row_32bit = input_dimension / 4;

    if (accelerator_handle->execution_mode == ACCELERATOR_MODE_DIRECT_MEMORY_ACCESS) {
        uint32_t *input_buffer_words = (uint32_t *)((uint8_t *)accelerator_handle->dma_virtual_buffer_address + DMA_BUFFER_INPUT_OFFSET_BYTES);
        const uint32_t *input_source_words = (const uint32_t *)input_activations;
        for (size_t word_index = 0; word_index < words_per_neuron_row_32bit; word_index++) {
            input_buffer_words[word_index] = input_source_words[word_index];
        }

        uint32_t *weights_buffer_words = (uint32_t *)accelerator_handle->dma_virtual_buffer_address;

        for (size_t neuron_output_index = 0; neuron_output_index < output_dimension; neuron_output_index++) {
            const uint32_t *weight_row_words = (const uint32_t *)(weight_matrix + (neuron_output_index * input_dimension));
            for (size_t word_index = 0; word_index < words_per_neuron_row_32bit; word_index++) {
                weights_buffer_words[word_index] = weight_row_words[word_index];
            }

            accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_SOURCE_ADDRESS_A] = accelerator_handle->dma_physical_buffer_address_a;
            accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_SOURCE_ADDRESS_B] = accelerator_handle->dma_physical_buffer_address_b;
            accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_TRANSFER_LENGTH]   = (uint32_t)words_per_neuron_row_32bit;
            accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_CONTROL]              = CONTROL_BIT_START_DIRECT_MEMORY_ACCESS;

            while (!((accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_STATUS] >> 4) & 0x01));
            int32_t accumulator_value = (int32_t)accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_ACCUMULATOR_OUTPUT];

            if (bias_vector) accumulator_value += bias_vector[neuron_output_index];

            int32_t scaled_activation_value = (requantization_shift_bits > 0) 
                ? ((accumulator_value + (1 << (requantization_shift_bits - 1))) >> requantization_shift_bits) 
                : accumulator_value;
            if (enable_relu_activation && scaled_activation_value < 0) scaled_activation_value = 0;
            if (scaled_activation_value > 127)  scaled_activation_value = 127;
            if (scaled_activation_value < -128) scaled_activation_value = -128;

            output_activations[neuron_output_index] = (int8_t)scaled_activation_value;
        }
    } else {
        for (size_t neuron_output_index = 0; neuron_output_index < output_dimension; neuron_output_index++) {
            const int8_t *current_weight_row = weight_matrix + (neuron_output_index * input_dimension);
            int32_t accumulator_value = accelerator_compute_vector_dot_product(accelerator_handle, current_weight_row, input_activations, input_dimension);
            if (bias_vector) accumulator_value += bias_vector[neuron_output_index];

            int32_t scaled_activation_value = (requantization_shift_bits > 0) 
                ? ((accumulator_value + (1 << (requantization_shift_bits - 1))) >> requantization_shift_bits) 
                : accumulator_value;
            if (enable_relu_activation && scaled_activation_value < 0) scaled_activation_value = 0;
            if (scaled_activation_value > 127)  scaled_activation_value = 127;
            if (scaled_activation_value < -128) scaled_activation_value = -128;

            output_activations[neuron_output_index] = (int8_t)scaled_activation_value;
        }
    }
}

uint32_t accelerator_preload_weight_matrix_to_memory(accelerator_device_handle_t *accelerator_handle, const int8_t *weight_matrix, size_t matrix_size_bytes, uint32_t buffer_offset_bytes) {
    if (buffer_offset_bytes + matrix_size_bytes > accelerator_handle->dma_buffer_size_bytes) return 0;

    uint32_t *destination_words = (uint32_t *)((uint8_t *)accelerator_handle->dma_virtual_buffer_address + buffer_offset_bytes);
    const uint32_t *source_words = (const uint32_t *)weight_matrix;
    size_t total_words_32bit = matrix_size_bytes / 4;
    for (size_t word_index = 0; word_index < total_words_32bit; word_index++) {
        destination_words[word_index] = source_words[word_index];
    }
    size_t remaining_bytes_count = matrix_size_bytes % 4;
    uint8_t *destination_remaining_bytes = (uint8_t *)destination_words + (total_words_32bit * 4);
    const uint8_t *source_remaining_bytes = (const uint8_t *)source_words + (total_words_32bit * 4);
    for (size_t byte_index = 0; byte_index < remaining_bytes_count; byte_index++) {
        destination_remaining_bytes[byte_index] = source_remaining_bytes[byte_index];
    }

    return accelerator_handle->dma_physical_buffer_address_a + buffer_offset_bytes;
}

void accelerator_compute_dense_layer_zero_copy(accelerator_device_handle_t *accelerator_handle, 
                                               const int8_t *input_activations, 
                                               uint32_t physical_address_weights, 
                                               const int32_t *bias_vector, 
                                               int8_t *output_activations, 
                                               size_t input_dimension, 
                                               size_t output_dimension, 
                                               int requantization_shift_bits, 
                                               int enable_relu_activation) {
    size_t words_per_neuron_row_32bit = input_dimension / 4;

    uint32_t *input_buffer_words = (uint32_t *)((uint8_t *)accelerator_handle->dma_virtual_buffer_address + DMA_BUFFER_INPUT_OFFSET_BYTES);
    const uint32_t *input_source_words = (const uint32_t *)input_activations;
    for (size_t word_index = 0; word_index < words_per_neuron_row_32bit; word_index++) {
        input_buffer_words[word_index] = input_source_words[word_index];
    }
    uint32_t physical_address_input_buffer = accelerator_handle->dma_physical_buffer_address_b;

    for (size_t neuron_output_index = 0; neuron_output_index < output_dimension; neuron_output_index++) {
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_SOURCE_ADDRESS_A] = physical_address_weights + (uint32_t)(neuron_output_index * input_dimension);
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_SOURCE_ADDRESS_B] = physical_address_input_buffer;
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_DMA_TRANSFER_LENGTH]   = (uint32_t)words_per_neuron_row_32bit;
        accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_CONTROL]              = CONTROL_BIT_START_DIRECT_MEMORY_ACCESS;

        while (!((accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_STATUS] >> 4) & 0x01));
        int32_t accumulator_value = (int32_t)accelerator_handle->virtual_control_status_registers[REGISTER_OFFSET_ACCUMULATOR_OUTPUT];

        if (bias_vector) accumulator_value += bias_vector[neuron_output_index];

        int32_t scaled_activation_value = (requantization_shift_bits > 0) 
            ? ((accumulator_value + (1 << (requantization_shift_bits - 1))) >> requantization_shift_bits) 
            : accumulator_value;
        if (enable_relu_activation && scaled_activation_value < 0) scaled_activation_value = 0;
        if (scaled_activation_value > 127)  scaled_activation_value = 127;
        if (scaled_activation_value < -128) scaled_activation_value = -128;

        output_activations[neuron_output_index] = (int8_t)scaled_activation_value;
    }
}