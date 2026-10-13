library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vec_mac_unit is
    generic (
        VECTOR_ELEMENT_COUNT  : positive := 4;  -- 4 éléments INT8 par mot 32-bit
        ELEMENT_BIT_WIDTH     : positive := 8;  -- INT8
        ACCUMULATOR_BIT_WIDTH : positive := 32  -- Accumulateur 32 bits
    );
    port (
        clock                : in  std_logic;
        reset_active_low     : in  std_logic;
        enable               : in  std_logic;
        clear_accumulator    : in  std_logic;
        valid_input          : in  std_logic;
        
        -- vector_data_a_input = [ A3 (31..24) | A2 (23..16) | A1 (15..8) | A0 (7..0) ]
        vector_data_a_input  : in  std_logic_vector(31 downto 0);
        vector_data_b_input  : in  std_logic_vector(31 downto 0);
        
        valid_output         : out std_logic;
        accumulator_output   : out signed(ACCUMULATOR_BIT_WIDTH - 1 downto 0)
    );
end entity vec_mac_unit;

architecture rtl of vec_mac_unit is

    type operand_element_array_type is array (0 to VECTOR_ELEMENT_COUNT - 1) of signed(ELEMENT_BIT_WIDTH - 1 downto 0);
    type product_element_array_type is array (0 to VECTOR_ELEMENT_COUNT - 1) of signed((2 * ELEMENT_BIT_WIDTH) - 1 downto 0);

    signal activation_operand_registers       : operand_element_array_type := (others => (others => '0'));
    signal weight_operand_registers           : operand_element_array_type := (others => (others => '0'));

    signal product_element_registers          : product_element_array_type := (others => (others => '0'));

    -- Étage 2 de l'arbre d'addition combinatoire (implémenté sur tranches CARRY4)
    signal partial_sum_stage1_lower_pair      : signed((2 * ELEMENT_BIT_WIDTH) downto 0);      -- p0 + p1 (17 bits)
    signal partial_sum_stage1_upper_pair      : signed((2 * ELEMENT_BIT_WIDTH) downto 0);      -- p2 + p3 (17 bits)
    signal vector_four_elements_sum           : signed((2 * ELEMENT_BIT_WIDTH) + 1 downto 0);  -- sum_0 + sum_1 (18 bits)

    signal accumulator_sum_register           : signed(ACCUMULATOR_BIT_WIDTH - 1 downto 0) := (others => '0');

    signal valid_pipeline_shift_register      : std_logic_vector(2 downto 0) := (others => '0');
    signal clear_accumulator_pipeline_register: std_logic_vector(1 downto 0) := (others => '0');

    -- Directives Vivado pour forcer les DSP48E1 uniquement sur les 4 multiplications
    attribute use_dsp : string;
    attribute use_dsp of product_element_registers : signal is "yes";

begin

    process_vector_multiply_accumulate_pipeline : process(clock, reset_active_low)
    begin
        if reset_active_low = '0' then
            activation_operand_registers        <= (others => (others => '0'));
            weight_operand_registers            <= (others => (others => '0'));
            product_element_registers           <= (others => (others => '0'));
            accumulator_sum_register           <= (others => '0');
            valid_pipeline_shift_register       <= (others => '0');
            clear_accumulator_pipeline_register <= (others => '0');

        elsif rising_edge(clock) then
            if enable = '1' then
                -- Étage 1 : Décompactage et enregistrement des 4 octets INT8
                for element_index in 0 to VECTOR_ELEMENT_COUNT - 1 loop
                    activation_operand_registers(element_index) <= signed(vector_data_a_input((element_index + 1) * ELEMENT_BIT_WIDTH - 1 downto element_index * ELEMENT_BIT_WIDTH));
                    weight_operand_registers(element_index)     <= signed(vector_data_b_input((element_index + 1) * ELEMENT_BIT_WIDTH - 1 downto element_index * ELEMENT_BIT_WIDTH));
                end loop;

                -- Étage 2 : Multiplications parallèles 4x (4 blocs DSP48E1)
                for element_index in 0 to VECTOR_ELEMENT_COUNT - 1 loop
                    product_element_registers(element_index) <= activation_operand_registers(element_index) * weight_operand_registers(element_index);
                end loop;

                -- Étage 3 : Sommation et accumulation finale 32-bit
                if clear_accumulator_pipeline_register(1) = '1' then
                    if valid_pipeline_shift_register(1) = '1' then
                        accumulator_sum_register <= resize(vector_four_elements_sum, ACCUMULATOR_BIT_WIDTH);
                    else
                        accumulator_sum_register <= (others => '0');
                    end if;
                elsif valid_pipeline_shift_register(1) = '1' then
                    accumulator_sum_register <= accumulator_sum_register + resize(vector_four_elements_sum, ACCUMULATOR_BIT_WIDTH);
                end if;

                -- Propagation des drapeaux de validation et de remise à zéro
                valid_pipeline_shift_register        <= valid_pipeline_shift_register(1 downto 0) & valid_input;
                clear_accumulator_pipeline_register  <= clear_accumulator_pipeline_register(0) & clear_accumulator;
            end if;
        end if;
    end process process_vector_multiply_accumulate_pipeline;

    -- Arbre d'addition combinatoire (2 étages légers sur tranches logiques)
    partial_sum_stage1_lower_pair <= resize(product_element_registers(0), 17) + resize(product_element_registers(1), 17);
    partial_sum_stage1_upper_pair <= resize(product_element_registers(2), 17) + resize(product_element_registers(3), 17);
    vector_four_elements_sum      <= resize(partial_sum_stage1_lower_pair, 18) + resize(partial_sum_stage1_upper_pair, 18);

    accumulator_output <= accumulator_sum_register;
    valid_output       <= valid_pipeline_shift_register(2);

end architecture rtl;