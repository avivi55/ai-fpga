library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dma_reader is
    port (
        clock : in std_logic;
        reset_active_low : in std_logic;

        start_dma_transfer_input : in std_logic;
        source_address_a_word_input : in std_logic_vector(29 downto 0);
        source_address_b_word_input : in std_logic_vector(29 downto 0);
        transfer_length_words_input : in unsigned(15 downto 0);
        dma_busy_flag_output : out std_logic;
        dma_done_pulse_output : out std_logic;

        m_wb_cyc_o : out std_logic;
        m_wb_stb_o : out std_logic;
        m_wb_we_o : out std_logic;
        m_wb_sel_o : out std_logic_vector(3 downto 0);
        m_wb_adr_o : out std_logic_vector(29 downto 0);
        m_wb_dat_o : out std_logic_vector(31 downto 0);
        m_wb_dat_i : in std_logic_vector(31 downto 0);
        m_wb_ack_i : in std_logic;

        mac_valid_output : out std_logic;
        mac_clear_accumulator_output : out std_logic;
        mac_vector_a_data_output : out std_logic_vector(31 downto 0);
        mac_vector_b_data_output : out std_logic_vector(31 downto 0)
    );
end entity dma_reader;

architecture rtl of dma_reader is

    constant BURST_CHUNK_SIZE_WORDS : integer := 16;

    type dma_state_type is (
        STATE_IDLE,
        STATE_PREPARE_CHUNK,
        STATE_BURST_OPERAND_A_REQUEST,
        STATE_BURST_OPERAND_A_WAIT_ACK,
        STATE_BURST_OPERAND_B_REQUEST,
        STATE_BURST_OPERAND_B_WAIT_ACK,
        STATE_STREAM_TO_VECTOR_MAC,
        STATE_DRAIN_PIPELINE,
        STATE_TRANSFER_DONE
    );
    signal current_fsm_state : dma_state_type := STATE_IDLE;

    signal current_address_a_register : unsigned(29 downto 0) := (others => '0');
    signal current_address_b_register : unsigned(29 downto 0) := (others => '0');
    signal remaining_words_counter : unsigned(15 downto 0) := (others => '0');
    signal current_chunk_target_word_count : unsigned(4 downto 0) := (others => '0'); -- Taille du bloc courant (1..16)
    signal burst_words_read_counter : unsigned(4 downto 0) := (others => '0');
    signal stream_words_fed_counter : unsigned(4 downto 0) := (others => '0');
    signal is_first_word_of_transfer : std_logic := '0';

    signal fifo_a_write_enable : std_logic;
    signal fifo_a_data_write : std_logic_vector(31 downto 0);
    signal fifo_a_read_enable : std_logic;
    signal fifo_a_data_read : std_logic_vector(31 downto 0);
    signal fifo_a_empty_flag : std_logic;
    signal fifo_a_full_flag : std_logic;

    signal fifo_b_write_enable : std_logic;
    signal fifo_b_data_write : std_logic_vector(31 downto 0);
    signal fifo_b_read_enable : std_logic;
    signal fifo_b_data_read : std_logic_vector(31 downto 0);
    signal fifo_b_empty_flag : std_logic;
    signal fifo_b_full_flag : std_logic;

    signal pipeline_drain_cycle_counter : unsigned(2 downto 0) := (others => '0');

begin

    m_wb_we_o <= '0';
    m_wb_sel_o <= "1111";
    m_wb_dat_o <= (others => '0');

    mac_vector_a_data_output <= fifo_a_data_read;
    mac_vector_b_data_output <= fifo_b_data_read;

    instance_fifo_operand_a : entity work.sync_fifo
        generic map(
            DATA_BIT_WIDTH => 32,
            FIFO_DEPTH_WORDS => BURST_CHUNK_SIZE_WORDS
        )
        port map(
            clock => clock,
            reset_active_low => reset_active_low,
            write_enable_input => fifo_a_write_enable,
            data_write_input => fifo_a_data_write,
            fifo_full_flag_output => fifo_a_full_flag,
            read_enable_input => fifo_a_read_enable,
            data_read_output => fifo_a_data_read,
            fifo_empty_flag_output => fifo_a_empty_flag,
            occupancy_counter_output => open
        );

    instance_fifo_operand_b : entity work.sync_fifo
        generic map(
            DATA_BIT_WIDTH => 32,
            FIFO_DEPTH_WORDS => BURST_CHUNK_SIZE_WORDS
        )
        port map(
            clock => clock,
            reset_active_low => reset_active_low,
            write_enable_input => fifo_b_write_enable,
            data_write_input => fifo_b_data_write,
            fifo_full_flag_output => fifo_b_full_flag,
            read_enable_input => fifo_b_read_enable,
            data_read_output => fifo_b_data_read,
            fifo_empty_flag_output => fifo_b_empty_flag,
            occupancy_counter_output => open
        );

    process_dma_finite_state_machine : process (clock, reset_active_low)
    begin
        if reset_active_low = '0' then
            current_fsm_state <= STATE_IDLE;
            current_address_a_register <= (others => '0');
            current_address_b_register <= (others => '0');
            remaining_words_counter <= (others => '0');
            current_chunk_target_word_count <= (others => '0');
            burst_words_read_counter <= (others => '0');
            stream_words_fed_counter <= (others => '0');
            is_first_word_of_transfer <= '0';
            pipeline_drain_cycle_counter <= (others => '0');

            dma_busy_flag_output <= '0';
            dma_done_pulse_output <= '0';
            m_wb_cyc_o <= '0';
            m_wb_stb_o <= '0';
            m_wb_adr_o <= (others => '0');

            fifo_a_write_enable <= '0';
            fifo_a_data_write <= (others => '0');
            fifo_a_read_enable <= '0';

            fifo_b_write_enable <= '0';
            fifo_b_data_write <= (others => '0');
            fifo_b_read_enable <= '0';

            mac_valid_output <= '0';
            mac_clear_accumulator_output <= '0';

        elsif rising_edge(clock) then
            dma_done_pulse_output <= '0';
            fifo_a_write_enable <= '0';
            fifo_a_read_enable <= '0';
            fifo_b_write_enable <= '0';
            fifo_b_read_enable <= '0';
            mac_valid_output <= '0';
            mac_clear_accumulator_output <= '0';

            case current_fsm_state is
                when STATE_IDLE =>
                    dma_busy_flag_output <= '0';
                    m_wb_cyc_o <= '0';
                    m_wb_stb_o <= '0';

                    if start_dma_transfer_input = '1' then
                        if transfer_length_words_input = 0 then
                            dma_done_pulse_output <= '1';
                        else
                            current_address_a_register <= unsigned(source_address_a_word_input);
                            current_address_b_register <= unsigned(source_address_b_word_input);
                            remaining_words_counter <= transfer_length_words_input;
                            is_first_word_of_transfer <= '1';
                            dma_busy_flag_output <= '1';
                            current_fsm_state <= STATE_PREPARE_CHUNK;
                        end if;
                    end if;

                when STATE_PREPARE_CHUNK =>
                    dma_busy_flag_output <= '1';
                    if remaining_words_counter >= BURST_CHUNK_SIZE_WORDS then
                        current_chunk_target_word_count <= to_unsigned(BURST_CHUNK_SIZE_WORDS, 5);
                    else
                        current_chunk_target_word_count <= resize(remaining_words_counter(4 downto 0), 5);
                    end if;
                    burst_words_read_counter <= (others => '0');
                    stream_words_fed_counter <= (others => '0');
                    current_fsm_state <= STATE_BURST_OPERAND_A_REQUEST;

                when STATE_BURST_OPERAND_A_REQUEST =>
                    dma_busy_flag_output <= '1';
                    m_wb_cyc_o <= '1';
                    m_wb_stb_o <= '1';
                    m_wb_adr_o <= std_logic_vector(current_address_a_register);
                    current_fsm_state <= STATE_BURST_OPERAND_A_WAIT_ACK;

                when STATE_BURST_OPERAND_A_WAIT_ACK =>
                    dma_busy_flag_output <= '1';
                    if m_wb_ack_i = '1' then
                        fifo_a_write_enable <= '1';
                        fifo_a_data_write <= m_wb_dat_i;
                        current_address_a_register <= current_address_a_register + 1;
                        burst_words_read_counter <= burst_words_read_counter + 1;

                        if (burst_words_read_counter + 1 = current_chunk_target_word_count) then
                            m_wb_cyc_o <= '0';
                            m_wb_stb_o <= '0';
                            burst_words_read_counter <= (others => '0');
                            current_fsm_state <= STATE_BURST_OPERAND_B_REQUEST;
                        else
                            m_wb_stb_o <= '0';
                            current_fsm_state <= STATE_BURST_OPERAND_A_REQUEST;
                        end if;
                    end if;

                when STATE_BURST_OPERAND_B_REQUEST =>
                    dma_busy_flag_output <= '1';
                    m_wb_cyc_o <= '1';
                    m_wb_stb_o <= '1';
                    m_wb_adr_o <= std_logic_vector(current_address_b_register);
                    current_fsm_state <= STATE_BURST_OPERAND_B_WAIT_ACK;

                when STATE_BURST_OPERAND_B_WAIT_ACK =>
                    dma_busy_flag_output <= '1';
                    if m_wb_ack_i = '1' then
                        fifo_b_write_enable <= '1';
                        fifo_b_data_write <= m_wb_dat_i;
                        current_address_b_register <= current_address_b_register + 1;
                        burst_words_read_counter <= burst_words_read_counter + 1;

                        if (burst_words_read_counter + 1 = current_chunk_target_word_count) then
                            m_wb_cyc_o <= '0';
                            m_wb_stb_o <= '0';
                            burst_words_read_counter <= (others => '0');
                            stream_words_fed_counter <= (others => '0');
                            current_fsm_state <= STATE_STREAM_TO_VECTOR_MAC;
                        else
                            m_wb_stb_o <= '0';
                            current_fsm_state <= STATE_BURST_OPERAND_B_REQUEST;
                        end if;
                    end if;

                when STATE_STREAM_TO_VECTOR_MAC =>
                    dma_busy_flag_output <= '1';
                    mac_valid_output <= '1';
                    mac_clear_accumulator_output <= is_first_word_of_transfer;
                    is_first_word_of_transfer <= '0';

                    fifo_a_read_enable <= '1';
                    fifo_b_read_enable <= '1';
                    stream_words_fed_counter <= stream_words_fed_counter + 1;

                    if (stream_words_fed_counter + 1 = current_chunk_target_word_count) then
                        remaining_words_counter <= remaining_words_counter - resize(current_chunk_target_word_count, 16);

                        if (remaining_words_counter = resize(current_chunk_target_word_count, 16)) then
                            pipeline_drain_cycle_counter <= (others => '0');
                            current_fsm_state <= STATE_DRAIN_PIPELINE;
                        else
                            current_fsm_state <= STATE_PREPARE_CHUNK;
                        end if;
                    end if;

                when STATE_DRAIN_PIPELINE =>
                    dma_busy_flag_output <= '1';
                    if pipeline_drain_cycle_counter = 3 then
                        current_fsm_state <= STATE_TRANSFER_DONE;
                    else
                        pipeline_drain_cycle_counter <= pipeline_drain_cycle_counter + 1;
                    end if;

                when STATE_TRANSFER_DONE =>
                    dma_busy_flag_output <= '0';
                    dma_done_pulse_output <= '1';
                    current_fsm_state <= STATE_IDLE;

                when others =>
                    current_fsm_state <= STATE_IDLE;
            end case;
        end if;
    end process process_dma_finite_state_machine;

end architecture rtl;