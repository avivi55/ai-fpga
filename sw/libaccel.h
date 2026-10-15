#ifndef LIBACCEL_H
#define LIBACCEL_H

typedef unsigned int   uint32_t;
typedef int            int32_t;
typedef signed char    int8_t;
typedef unsigned char  uint8_t;
typedef unsigned long  size_t;

typedef enum {
    ACCELERATOR_MODE_PROGRAMMED_IO        = 0,
    ACCELERATOR_MODE_DIRECT_MEMORY_ACCESS = 1
} accelerator_execution_mode_t;

typedef struct {
    int                           file_descriptor_memory;
    volatile uint32_t            *virtual_control_status_registers;
    void                         *dma_virtual_buffer_address;
    uint32_t                      dma_physical_buffer_address_a;
    uint32_t                      dma_physical_buffer_address_b;
    size_t                        dma_buffer_size_bytes;
    accelerator_execution_mode_t  execution_mode;
} accelerator_device_handle_t;

int  accelerator_hardware_initialize(accelerator_device_handle_t *accelerator_handle, 
                                     accelerator_execution_mode_t execution_mode);
void accelerator_hardware_release(accelerator_device_handle_t *accelerator_handle);

int32_t accelerator_compute_vector_dot_product(accelerator_device_handle_t *accelerator_handle, 
                                               const int8_t *vector_operand_a, 
                                               const int8_t *vector_operand_b, 
                                               size_t vector_length_elements);
int32_t cpu_compute_vector_dot_product(const int8_t *vector_operand_a, 
                                       const int8_t *vector_operand_b, 
                                       size_t vector_length_elements);

void accelerator_compute_dense_neural_layer(accelerator_device_handle_t *accelerator_handle, 
                                            const int8_t *input_activations, 
                                            const int8_t *weight_matrix, 
                                            const int32_t *bias_vector, 
                                            int8_t *output_activations, 
                                            size_t input_dimension, 
                                            size_t output_dimension, 
                                            int requantization_shift_bits, 
                                            int enable_relu_activation);

uint32_t accelerator_preload_weight_matrix_to_memory(accelerator_device_handle_t *accelerator_handle, 
                                                     const int8_t *weight_matrix, 
                                                     size_t matrix_size_bytes, 
                                                     uint32_t buffer_offset_bytes);

void accelerator_compute_dense_layer_zero_copy(accelerator_device_handle_t *accelerator_handle, 
                                               const int8_t *input_activations, 
                                               uint32_t physical_address_weights, 
                                               const int32_t *bias_vector, 
                                               int8_t *output_activations, 
                                               size_t input_dimension, 
                                               size_t output_dimension, 
                                               int requantization_shift_bits, 
                                               int enable_relu_activation);

void cpu_compute_dense_neural_layer(const int8_t *input_activations, 
                                    const int8_t *weight_matrix, 
                                    const int32_t *bias_vector, 
                                    int8_t *output_activations, 
                                    size_t input_dimension, 
                                    size_t output_dimension, 
                                    int requantization_shift_bits, 
                                    int enable_relu_activation);

uint32_t accelerator_get_current_time_microseconds(void);

#endif /* LIBACCEL_H */