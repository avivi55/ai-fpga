library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mac_unit is
    generic (
        DATA_BIT_WIDTH        : positive := 8;
        ACCUMULATOR_BIT_WIDTH : positive := 32
    );
    port (
        clock                   : in  std_logic;
        reset_active_low        : in  std_logic;
        enable                  : in  std_logic;
        clear_accumulator       : in  std_logic;
        valid_input             : in  std_logic;
        activation_data_input   : in  signed(DATA_BIT_WIDTH - 1 downto 0); -- Activation INT8
        weight_data_input       : in  signed(DATA_BIT_WIDTH - 1 downto 0); -- Poids INT8
        
        valid_output            : out std_logic;
        accumulator_output      : out signed(ACCUMULATOR_BIT_WIDTH - 1 downto 0)
    );
end entity mac_unit;

architecture rtl of mac_unit is

    signal activation_operand_register           : signed(DATA_BIT_WIDTH - 1 downto 0) := (others => '0');
    signal weight_operand_register               : signed(DATA_BIT_WIDTH - 1 downto 0) := (others => '0');
    
    signal multiplication_product_register       : signed((2 * DATA_BIT_WIDTH) - 1 downto 0) := (others => '0');
    signal accumulator_sum_register              : signed(ACCUMULATOR_BIT_WIDTH - 1 downto 0) := (others => '0');
    
    signal valid_pipeline_shift_register         : std_logic_vector(2 downto 0) := (others => '0');
    signal clear_accumulator_pipeline_register   : std_logic_vector(1 downto 0) := (others => '0');

    -- for vivado
    attribute use_dsp : string;
    attribute use_dsp of rtl : architecture is "yes";

begin

    process_multiply_accumulate : process(clock, reset_active_low)
    begin
        if reset_active_low = '0' then
            activation_operand_register         <= (others => '0');
            weight_operand_register             <= (others => '0');
            multiplication_product_register     <= (others => '0');
            accumulator_sum_register            <= (others => '0');
            valid_pipeline_shift_register       <= (others => '0');
            clear_accumulator_pipeline_register <= (others => '0');
            
        elsif rising_edge(clock) then
            if enable = '1' then
                activation_operand_register <= activation_data_input;
                weight_operand_register     <= weight_data_input;
                
                multiplication_product_register <= activation_operand_register * weight_operand_register;
                
                if clear_accumulator_pipeline_register(1) = '1' then
                    if valid_pipeline_shift_register(1) = '1' then
                        accumulator_sum_register <= resize(multiplication_product_register, ACCUMULATOR_BIT_WIDTH);
                    else
                        accumulator_sum_register <= (others => '0');
                    end if;
                elsif valid_pipeline_shift_register(1) = '1' then
                    accumulator_sum_register <= accumulator_sum_register + resize(multiplication_product_register, ACCUMULATOR_BIT_WIDTH);
                end if;

                valid_pipeline_shift_register       <= valid_pipeline_shift_register(1 downto 0) & valid_input;
                clear_accumulator_pipeline_register <= clear_accumulator_pipeline_register(0) & clear_accumulator;
            end if;
        end if;
    end process process_multiply_accumulate;

    accumulator_output <= accumulator_sum_register;
    valid_output       <= valid_pipeline_shift_register(2);

end architecture rtl;