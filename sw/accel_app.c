typedef unsigned int   uint32_t;
typedef int            int32_t;
typedef signed char    int8_t;
typedef unsigned char  uint8_t;
typedef unsigned long  size_t;

#define LINUX_SYSTEM_CALL_NUMBER_WRITE            64
#define LINUX_SYSTEM_CALL_NUMBER_EXIT             93
#define LINUX_SYSTEM_CALL_NUMBER_MMAP2            222
#define LINUX_SYSTEM_CALL_NUMBER_OPENAT           56
#define LINUX_SYSTEM_CALL_NUMBER_CLOCK_GETTIME64  403

#define FILE_OPEN_FLAG_READ_WRITE                 00000002
#define FILE_OPEN_FLAG_SYNCHRONOUS_IO             00010000
#define MEMORY_PROTECTION_READ                    0x1
#define MEMORY_PROTECTION_WRITE                   0x2
#define MEMORY_MAP_SHARED                         0x01
#define FILE_DESCRIPTOR_CURRENT_WORKING_DIRECTORY -100
#define POSIX_CLOCK_MONOTONIC                     1

static inline long system_call_1_argument(long syscall_number, long argument_0) {
    register long system_call_register_a7 asm("a7") = syscall_number;
    register long system_call_register_a0 asm("a0") = argument_0;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a7) : "memory");
    return system_call_register_a0;
}

static inline long system_call_2_arguments(long syscall_number, long argument_0, long argument_1) {
    register long system_call_register_a7 asm("a7") = syscall_number;
    register long system_call_register_a0 asm("a0") = argument_0;
    register long system_call_register_a1 asm("a1") = argument_1;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a1), "r"(system_call_register_a7) : "memory");
    return system_call_register_a0;
}

static inline long system_call_3_arguments(long syscall_number, long argument_0, long argument_1, long argument_2) {
    register long system_call_register_a7 asm("a7") = syscall_number;
    register long system_call_register_a0 asm("a0") = argument_0;
    register long system_call_register_a1 asm("a1") = argument_1;
    register long system_call_register_a2 asm("a2") = argument_2;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a1), "r"(system_call_register_a2), "r"(system_call_register_a7) : "memory");
    return system_call_register_a0;
}

static inline long system_call_4_arguments(long syscall_number, long argument_0, long argument_1, long argument_2, long argument_3) {
    register long system_call_register_a7 asm("a7") = syscall_number;
    register long system_call_register_a0 asm("a0") = argument_0;
    register long system_call_register_a1 asm("a1") = argument_1;
    register long system_call_register_a2 asm("a2") = argument_2;
    register long system_call_register_a3 asm("a3") = argument_3;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a1), "r"(system_call_register_a2), "r"(system_call_register_a3), "r"(system_call_register_a7) : "memory");
    return system_call_register_a0;
}

static inline long system_call_6_arguments(long syscall_number, long argument_0, long argument_1, long argument_2, long argument_3, long argument_4, long argument_5) {
    register long system_call_register_a7 asm("a7") = syscall_number;
    register long system_call_register_a0 asm("a0") = argument_0;
    register long system_call_register_a1 asm("a1") = argument_1;
    register long system_call_register_a2 asm("a2") = argument_2;
    register long system_call_register_a3 asm("a3") = argument_3;
    register long system_call_register_a4 asm("a4") = argument_4;
    register long system_call_register_a5 asm("a5") = argument_5;
    asm volatile("ecall" : "+r"(system_call_register_a0) : "r"(system_call_register_a1), "r"(system_call_register_a2), "r"(system_call_register_a3), "r"(system_call_register_a4), "r"(system_call_register_a5), "r"(system_call_register_a7) : "memory");
    return system_call_register_a0;
}

struct linux_timespec64_structure {
    uint32_t seconds_lower_word;
    uint32_t seconds_upper_word;
    uint32_t nanoseconds_lower_word;
    uint32_t nanoseconds_upper_word;
};

static inline uint32_t get_current_time_microseconds(void) {
    struct linux_timespec64_structure timestamp_structure = {0, 0, 0, 0};
    system_call_2_arguments(LINUX_SYSTEM_CALL_NUMBER_CLOCK_GETTIME64, POSIX_CLOCK_MONOTONIC, (long)&timestamp_structure);
    return (timestamp_structure.seconds_lower_word * 1000000) + (timestamp_structure.nanoseconds_lower_word / 1000);
}

static size_t calculate_string_length(const char *string_pointer) {
    size_t string_length_counter = 0;
    while (string_pointer[string_length_counter]) {
        string_length_counter++;
    }
    return string_length_counter;
}

static void print_string_to_standard_output(const char *string_pointer) {
    system_call_3_arguments(LINUX_SYSTEM_CALL_NUMBER_WRITE, 1, (long)string_pointer, calculate_string_length(string_pointer));
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
    system_call_3_arguments(LINUX_SYSTEM_CALL_NUMBER_WRITE, 1, (long)reversed_character_buffer, character_index);
}

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

static int32_t cpu_compute_vector_dot_product(const int8_t *vector_operand_a, 
                                              const int8_t *vector_operand_b, 
                                              size_t vector_length_elements) {
    int32_t accumulator_sum = 0;
    for (size_t element_index = 0; element_index < vector_length_elements; element_index++) {
        accumulator_sum += (int32_t)vector_operand_a[element_index] * (int32_t)vector_operand_b[element_index];
    }
    return accumulator_sum;
}

static int32_t accelerator_compute_vector_dot_product_programmed_io(volatile uint32_t *virtual_control_status_registers, 
                                                                    const int8_t *vector_operand_a, 
                                                                    const int8_t *vector_operand_b, 
                                                                    size_t vector_length_elements) {
    const uint32_t *vector_operand_a_words_32bit = (const uint32_t *)vector_operand_a;
    const uint32_t *vector_operand_b_words_32bit = (const uint32_t *)vector_operand_b;
    size_t transfer_length_words_32bit = vector_length_elements / 4;

    virtual_control_status_registers[REGISTER_OFFSET_DATA_A] = vector_operand_a_words_32bit[0];
    virtual_control_status_registers[REGISTER_OFFSET_DATA_B] = vector_operand_b_words_32bit[0];
    virtual_control_status_registers[REGISTER_OFFSET_CONTROL]  = (CONTROL_BIT_START_PROGRAMMED_IO | CONTROL_BIT_CLEAR_ACCUMULATOR);

    for (size_t word_index = 1; word_index < transfer_length_words_32bit; word_index++) {
        virtual_control_status_registers[REGISTER_OFFSET_DATA_A] = vector_operand_a_words_32bit[word_index];
        virtual_control_status_registers[REGISTER_OFFSET_DATA_B] = vector_operand_b_words_32bit[word_index];
        virtual_control_status_registers[REGISTER_OFFSET_CONTROL]  = CONTROL_BIT_START_PROGRAMMED_IO;
    }

    while (virtual_control_status_registers[REGISTER_OFFSET_STATUS] & STATUS_BIT_BUSY_PROGRAMMED_IO);

    return (int32_t)virtual_control_status_registers[REGISTER_OFFSET_ACCUMULATOR_OUTPUT];
}

static int32_t accelerator_compute_vector_dot_product_direct_memory_access(volatile uint32_t *virtual_control_status_registers, 
                                                                           uint32_t physical_address_buffer_a, 
                                                                           uint32_t physical_address_buffer_b, 
                                                                           size_t vector_length_elements) {
    size_t transfer_length_words_32bit = vector_length_elements / 4;

    virtual_control_status_registers[REGISTER_OFFSET_DMA_SOURCE_ADDRESS_A] = physical_address_buffer_a;
    virtual_control_status_registers[REGISTER_OFFSET_DMA_SOURCE_ADDRESS_B] = physical_address_buffer_b;
    virtual_control_status_registers[REGISTER_OFFSET_DMA_TRANSFER_LENGTH]   = (uint32_t)transfer_length_words_32bit;
    virtual_control_status_registers[REGISTER_OFFSET_CONTROL]              = CONTROL_BIT_START_DIRECT_MEMORY_ACCESS;

    while (!((virtual_control_status_registers[REGISTER_OFFSET_STATUS] >> 4) & 0x01));

    return (int32_t)virtual_control_status_registers[REGISTER_OFFSET_ACCUMULATOR_OUTPUT];
}

void _start(void) {
    print_string_to_standard_output("benchmark: 4096 INT8 elements (1000 iters)\n");

    const char memory_device_file_path[] = "/dev/mem";
    int file_descriptor_memory = system_call_4_arguments(
        LINUX_SYSTEM_CALL_NUMBER_OPENAT, 
        FILE_DESCRIPTOR_CURRENT_WORKING_DIRECTORY, 
        (long)memory_device_file_path, 
        FILE_OPEN_FLAG_READ_WRITE | FILE_OPEN_FLAG_SYNCHRONOUS_IO, 
        0
    );
    if (file_descriptor_memory < 0) {
        print_string_to_standard_output("error: open /dev/mem failed\n");
        system_call_1_argument(LINUX_SYSTEM_CALL_NUMBER_EXIT, 1);
    }

    void *virtual_mapped_registers = (void *)system_call_6_arguments(
        LINUX_SYSTEM_CALL_NUMBER_MMAP2, 
        0, 
        4096, 
        MEMORY_PROTECTION_READ | MEMORY_PROTECTION_WRITE, 
        MEMORY_MAP_SHARED, 
        file_descriptor_memory, 
        0x80002
    );
    if ((unsigned long)virtual_mapped_registers >= 0xfffff000UL) {
        print_string_to_standard_output("error: mmap registers failed\n");
        system_call_1_argument(LINUX_SYSTEM_CALL_NUMBER_EXIT, 2);
    }
    volatile uint32_t *virtual_control_status_registers = (volatile uint32_t *)virtual_mapped_registers;

    #define PHYSICAL_ADDRESS_DMA_BUFFER_A   0x48000000
    #define PHYSICAL_ADDRESS_DMA_BUFFER_B   0x48001000
    void *virtual_mapped_dma_buffer = (void *)system_call_6_arguments(
        LINUX_SYSTEM_CALL_NUMBER_MMAP2, 
        0, 
        16384, 
        MEMORY_PROTECTION_READ | MEMORY_PROTECTION_WRITE, 
        MEMORY_MAP_SHARED, 
        file_descriptor_memory, 
        0x48000
    );
    if ((unsigned long)virtual_mapped_dma_buffer >= 0xfffff000UL) {
        print_string_to_standard_output("error: mmap dma buffer failed\n");
        system_call_1_argument(LINUX_SYSTEM_CALL_NUMBER_EXIT, 3);
    }
    print_string_to_standard_output("csrs @ 0x80002000 | dma @ 0x48000000\n\n");

    #define VECTOR_LENGTH_ELEMENTS 4096
    static int8_t vector_operand_a[VECTOR_LENGTH_ELEMENTS];
    static int8_t vector_operand_b[VECTOR_LENGTH_ELEMENTS];

    int8_t *dma_buffer_pointer_a = (int8_t *)virtual_mapped_dma_buffer;
    int8_t *dma_buffer_pointer_b = (int8_t *)((uint8_t *)virtual_mapped_dma_buffer + 4096);

    for (int element_index = 0; element_index < VECTOR_LENGTH_ELEMENTS; element_index++) {
        vector_operand_a[element_index] = (int8_t)((element_index % 7) - 3);
        vector_operand_b[element_index] = (int8_t)((element_index % 5) - 2);
        dma_buffer_pointer_a[element_index] = vector_operand_a[element_index];
        dma_buffer_pointer_b[element_index] = vector_operand_b[element_index];
    }

    #define BENCHMARK_ITERATIONS_COUNT 1000

    // CPU
    uint32_t timestamp_cpu_start_microseconds = get_current_time_microseconds();
    int32_t calculation_result_cpu = 0;
    for (int benchmark_iteration_index = 0; benchmark_iteration_index < BENCHMARK_ITERATIONS_COUNT; benchmark_iteration_index++) {
        calculation_result_cpu = cpu_compute_vector_dot_product(vector_operand_a, vector_operand_b, VECTOR_LENGTH_ELEMENTS);
    }
    uint32_t timestamp_cpu_end_microseconds = get_current_time_microseconds();
    uint32_t duration_cpu_microseconds = (timestamp_cpu_end_microseconds >= timestamp_cpu_start_microseconds) 
        ? (timestamp_cpu_end_microseconds - timestamp_cpu_start_microseconds) 
        : 1;

    // PIO
    uint32_t timestamp_pio_start_microseconds = get_current_time_microseconds();
    int32_t calculation_result_programmed_io = 0;
    for (int benchmark_iteration_index = 0; benchmark_iteration_index < BENCHMARK_ITERATIONS_COUNT; benchmark_iteration_index++) {
        calculation_result_programmed_io = accelerator_compute_vector_dot_product_programmed_io(virtual_control_status_registers, vector_operand_a, vector_operand_b, VECTOR_LENGTH_ELEMENTS);
    }
    uint32_t timestamp_pio_end_microseconds = get_current_time_microseconds();
    uint32_t duration_programmed_io_microseconds = (timestamp_pio_end_microseconds >= timestamp_pio_start_microseconds) 
        ? (timestamp_pio_end_microseconds - timestamp_pio_start_microseconds) 
        : 1;

    // DMA
    uint32_t timestamp_dma_start_microseconds = get_current_time_microseconds();
    int32_t calculation_result_direct_memory_access = 0;
    for (int benchmark_iteration_index = 0; benchmark_iteration_index < BENCHMARK_ITERATIONS_COUNT; benchmark_iteration_index++) {
        calculation_result_direct_memory_access = accelerator_compute_vector_dot_product_direct_memory_access(virtual_control_status_registers, PHYSICAL_ADDRESS_DMA_BUFFER_A, PHYSICAL_ADDRESS_DMA_BUFFER_B, VECTOR_LENGTH_ELEMENTS);
    }
    uint32_t timestamp_dma_end_microseconds = get_current_time_microseconds();
    uint32_t duration_direct_memory_access_microseconds = (timestamp_dma_end_microseconds >= timestamp_dma_start_microseconds) 
        ? (timestamp_dma_end_microseconds - timestamp_dma_start_microseconds) 
        : 1;

    // Results
    print_string_to_standard_output("cpu : res=");
    print_decimal_integer_to_standard_output(calculation_result_cpu);
    print_string_to_standard_output(" | ");
    print_decimal_integer_to_standard_output(duration_cpu_microseconds);
    print_string_to_standard_output(" us\n");

    print_string_to_standard_output("pio : res=");
    print_decimal_integer_to_standard_output(calculation_result_programmed_io);
    print_string_to_standard_output(" | ");
    print_decimal_integer_to_standard_output(duration_programmed_io_microseconds);
    print_string_to_standard_output(" us (");
    if (duration_programmed_io_microseconds > 0) {
        uint32_t speedup_ratio_programmed_io_vs_cpu_x10 = (duration_cpu_microseconds * 10) / duration_programmed_io_microseconds;
        print_decimal_integer_to_standard_output(speedup_ratio_programmed_io_vs_cpu_x10 / 10);
        print_string_to_standard_output(".");
        print_decimal_integer_to_standard_output(speedup_ratio_programmed_io_vs_cpu_x10 % 10);
        print_string_to_standard_output("x");
    }
    print_string_to_standard_output(")\n");

    print_string_to_standard_output("dma : res=");
    print_decimal_integer_to_standard_output(calculation_result_direct_memory_access);
    print_string_to_standard_output(" | ");
    print_decimal_integer_to_standard_output(duration_direct_memory_access_microseconds);
    print_string_to_standard_output(" us (");
    if (duration_direct_memory_access_microseconds > 0) {
        uint32_t speedup_ratio_direct_memory_access_vs_cpu_x10 = (duration_cpu_microseconds * 10) / duration_direct_memory_access_microseconds;
        print_decimal_integer_to_standard_output(speedup_ratio_direct_memory_access_vs_cpu_x10 / 10);
        print_string_to_standard_output(".");
        print_decimal_integer_to_standard_output(speedup_ratio_direct_memory_access_vs_cpu_x10 % 10);
        print_string_to_standard_output("x");
    }
    print_string_to_standard_output(")\n\n");

    if (calculation_result_cpu == calculation_result_programmed_io && 
        calculation_result_cpu == calculation_result_direct_memory_access) {
        print_string_to_standard_output("status: match\n");
    } else {
        print_string_to_standard_output("status: mismatch\n");
    }

    system_call_1_argument(LINUX_SYSTEM_CALL_NUMBER_EXIT, 0);
}