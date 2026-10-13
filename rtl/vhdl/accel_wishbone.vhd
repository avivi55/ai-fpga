library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity accel_wishbone is
    port (
        -- Signaux de bus Wishbone Esclave normalisés (CPU vers Registres CSRs)
        wb_clk_i   : in  std_logic;
        wb_rst_i   : in  std_logic; -- Reset actif HAUT
        wb_cyc_i   : in  std_logic;
        wb_stb_i   : in  std_logic;
        wb_we_i    : in  std_logic;
        wb_sel_i   : in  std_logic_vector(3 downto 0);
        wb_adr_i   : in  std_logic_vector(3 downto 0); -- Adressage par mot (0 à 7)
        wb_dat_i   : in  std_logic_vector(31 downto 0);
        wb_dat_o   : out std_logic_vector(31 downto 0);
        wb_ack_o   : out std_logic;

        -- Signal d'interruption vers le contrôleur PLIC du processeur RISC-V
        irq_o      : out std_logic;

        -- Port Wishbone Maître normalisé (DMA vers Crossbar LiteX / DDR3)
        m_wb_cyc_o : out std_logic;
        m_wb_stb_o : out std_logic;
        m_wb_we_o  : out std_logic;
        m_wb_sel_o : out std_logic_vector(3 downto 0);
        m_wb_adr_o : out std_logic_vector(29 downto 0);
        m_wb_dat_o : out std_logic_vector(31 downto 0);
        m_wb_dat_i : in  std_logic_vector(31 downto 0);
        m_wb_ack_i : in  std_logic
    );
end entity accel_wishbone;

architecture rtl of accel_wishbone is

    -- Constantes d'adresses des registres CSR (Adressage par mot 32-bit)
    constant ADDRESS_REGISTER_CONTROL          : std_logic_vector(3 downto 0) := "0000"; -- 0 (0x00)
    constant ADDRESS_REGISTER_STATUS           : std_logic_vector(3 downto 0) := "0001"; -- 1 (0x04)
    constant ADDRESS_REGISTER_DATA_A           : std_logic_vector(3 downto 0) := "0010"; -- 2 (0x08)
    constant ADDRESS_REGISTER_DATA_B           : std_logic_vector(3 downto 0) := "0011"; -- 3 (0x0C)
    constant ADDRESS_REGISTER_ACCUMULATOR_OUT  : std_logic_vector(3 downto 0) := "0100"; -- 4 (0x10)
    constant ADDRESS_REGISTER_DMA_SOURCE_A     : std_logic_vector(3 downto 0) := "0101"; -- 5 (0x14)
    constant ADDRESS_REGISTER_DMA_SOURCE_B     : std_logic_vector(3 downto 0) := "0110"; -- 6 (0x18)
    constant ADDRESS_REGISTER_DMA_LENGTH       : std_logic_vector(3 downto 0) := "0111"; -- 7 (0x1C)

    -- Registres de commande et configuration (CSRs)
    signal register_control_irq_enable         : std_logic := '0';
    signal register_data_operand_a             : std_logic_vector(31 downto 0) := (others => '0');
    signal register_data_operand_b             : std_logic_vector(31 downto 0) := (others => '0');
    signal register_dma_source_address_a       : std_logic_vector(31 downto 0) := (others => '0');
    signal register_dma_source_address_b       : std_logic_vector(31 downto 0) := (others => '0');
    signal register_dma_transfer_length        : std_logic_vector(31 downto 0) := (others => '0');

    -- Signaux de synchronisation et déclenchement d'impulsions
    signal pulse_start_programmed_io           : std_logic := '0';
    signal pulse_clear_accumulator             : std_logic := '0';
    signal pulse_start_dma_transfer            : std_logic := '0';
    signal flag_done_programmed_io             : std_logic := '0';
    signal flag_done_dma_transfer              : std_logic := '0';
    signal interrupt_request_internal_register : std_logic := '0';

    -- Signaux d'état et flux de données provenant du lecteur DMA
    signal dma_busy_flag                       : std_logic;
    signal dma_done_pulse                      : std_logic;
    signal dma_mac_valid_signal                : std_logic;
    signal dma_mac_clear_signal                : std_logic;
    signal dma_mac_vector_a_signal             : std_logic_vector(31 downto 0);
    signal dma_mac_vector_b_signal             : std_logic_vector(31 downto 0);

    -- Signaux d'entrée multiplexés vers le processeur vectoriel vec_mac_unit
    signal mac_reset_active_low                : std_logic;
    signal mac_valid_input_multiplexed         : std_logic;
    signal mac_clear_accumulator_multiplexed   : std_logic;
    signal mac_vector_a_input_multiplexed      : std_logic_vector(31 downto 0);
    signal mac_vector_b_input_multiplexed      : std_logic_vector(31 downto 0);
    signal mac_valid_output_signal             : std_logic;
    signal mac_accumulator_output_signal       : signed(31 downto 0);

    -- Compteur de cycles BUSY pour le mode Programmed I/O direct (3 cycles de latence)
    signal busy_counter_programmed_io          : unsigned(1 downto 0) := (others => '0');
    signal is_busy_programmed_io               : std_logic;

begin

    mac_reset_active_low <= not wb_rst_i;
    irq_o                <= interrupt_request_internal_register and register_control_irq_enable;

    is_busy_programmed_io <= '1' when (busy_counter_programmed_io /= 0 or pulse_start_programmed_io = '1') else '0';

    ----------------------------------------------------------------------------
    -- Multiplexage des entrées de vec_mac_unit (Priorité absolue au streaming DMA)
    ----------------------------------------------------------------------------
    mac_valid_input_multiplexed       <= dma_mac_valid_signal    when (dma_busy_flag = '1') else pulse_start_programmed_io;
    mac_clear_accumulator_multiplexed <= dma_mac_clear_signal    when (dma_busy_flag = '1') else pulse_clear_accumulator;
    mac_vector_a_input_multiplexed    <= dma_mac_vector_a_signal when (dma_busy_flag = '1') else register_data_operand_a;
    mac_vector_b_input_multiplexed    <= dma_mac_vector_b_signal when (dma_busy_flag = '1') else register_data_operand_b;

    ----------------------------------------------------------------------------
    -- Instanciation du lecteur DMA Maître Wishbone
    ----------------------------------------------------------------------------
    instance_dma_reader : entity work.dma_reader
        port map (
            clock                        => wb_clk_i,
            reset_active_low             => mac_reset_active_low,

            start_dma_transfer_input     => pulse_start_dma_transfer,
            -- Conversion de l'adresse octet physique 32b vers adresse mot Wishbone 30b
            source_address_a_word_input  => register_dma_source_address_a(31 downto 2),
            source_address_b_word_input  => register_dma_source_address_b(31 downto 2),
            transfer_length_words_input  => unsigned(register_dma_transfer_length(15 downto 0)),
            dma_busy_flag_output         => dma_busy_flag,
            dma_done_pulse_output        => dma_done_pulse,

            m_wb_cyc_o                   => m_wb_cyc_o,
            m_wb_stb_o                   => m_wb_stb_o,
            m_wb_we_o                    => m_wb_we_o,
            m_wb_sel_o                   => m_wb_sel_o,
            m_wb_adr_o                   => m_wb_adr_o,
            m_wb_dat_o                   => m_wb_dat_o,
            m_wb_dat_i                   => m_wb_dat_i,
            m_wb_ack_i                   => m_wb_ack_i,

            mac_valid_output             => dma_mac_valid_signal,
            mac_clear_accumulator_output => dma_mac_clear_signal,
            mac_vector_a_data_output     => dma_mac_vector_a_signal,
            mac_vector_b_data_output     => dma_mac_vector_b_signal
        );

    ----------------------------------------------------------------------------
    -- Instanciation du processeur vectoriel 4x MAC INT8
    ----------------------------------------------------------------------------
    instance_vector_mac_unit : entity work.vec_mac_unit
        generic map (
            VECTOR_ELEMENT_COUNT  => 4,
            ELEMENT_BIT_WIDTH     => 8,
            ACCUMULATOR_BIT_WIDTH => 32
        )
        port map (
            clock                 => wb_clk_i,
            reset_active_low      => mac_reset_active_low,
            enable                => '1',
            clear_accumulator     => mac_clear_accumulator_multiplexed,
            valid_input           => mac_valid_input_multiplexed,
            vector_data_a_input   => mac_vector_a_input_multiplexed,
            vector_data_b_input   => mac_vector_b_input_multiplexed,
            valid_output          => mac_valid_output_signal,
            accumulator_output    => mac_accumulator_output_signal
        );

    ----------------------------------------------------------------------------
    -- Processus synchrone de gestion des transactions Wishbone Esclave (CSRs)
    ----------------------------------------------------------------------------
    process_wishbone_slave_bus_registers : process(wb_clk_i)
    begin
        if rising_edge(wb_clk_i) then
            if wb_rst_i = '1' then
                wb_ack_o                            <= '0';
                wb_dat_o                            <= (others => '0');
                register_control_irq_enable         <= '0';
                register_data_operand_a             <= (others => '0');
                register_data_operand_b             <= (others => '0');
                register_dma_source_address_a       <= (others => '0');
                register_dma_source_address_b       <= (others => '0');
                register_dma_transfer_length        <= (others => '0');
                pulse_start_programmed_io           <= '0';
                pulse_clear_accumulator             <= '0';
                pulse_start_dma_transfer            <= '0';
                flag_done_programmed_io             <= '0';
                flag_done_dma_transfer              <= '0';
                interrupt_request_internal_register <= '0';
                busy_counter_programmed_io          <= (others => '0');
            else
                -- Strobes remis à zéro par défaut
                wb_ack_o                  <= '0';
                pulse_start_programmed_io <= '0';
                pulse_clear_accumulator   <= '0';
                pulse_start_dma_transfer  <= '0';

                -- Positionnement du drapeau DONE en mode PIO
                if (dma_busy_flag = '0' and mac_valid_output_signal = '1') then
                    flag_done_programmed_io             <= '1';
                    interrupt_request_internal_register <= '1';
                end if;

                -- Positionnement du drapeau DONE en mode DMA
                if dma_done_pulse = '1' then
                    flag_done_dma_transfer              <= '1';
                    interrupt_request_internal_register <= '1';
                end if;

                -- Décrémentation du compteur d'occupation PIO
                if pulse_start_programmed_io = '1' then
                    busy_counter_programmed_io <= "11";
                elsif busy_counter_programmed_io /= 0 then
                    busy_counter_programmed_io <= busy_counter_programmed_io - 1;
                end if;

                -- Traitement d'un cycle de transaction Wishbone valide sur les CSRs
                if (wb_cyc_i = '1' and wb_stb_i = '1' and wb_ack_o = '0') then
                    wb_ack_o <= '1'; -- Acquittement sans cycle d'attente (0 wait-state)

                    if wb_we_i = '1' then
                        -- ==================== ÉCRITURE CSR ====================
                        case wb_adr_i is
                            when ADDRESS_REGISTER_CONTROL =>
                                pulse_start_programmed_io   <= wb_dat_i(0);
                                pulse_clear_accumulator     <= wb_dat_i(1);
                                register_control_irq_enable <= wb_dat_i(2);
                                pulse_start_dma_transfer    <= wb_dat_i(3); -- Bit 3 = START DMA
                                if wb_dat_i(0) = '1' then
                                    flag_done_programmed_io <= '0';
                                end if;
                                if wb_dat_i(3) = '1' then
                                    flag_done_dma_transfer  <= '0';
                                end if;

                            when ADDRESS_REGISTER_DATA_A =>
                                register_data_operand_a <= wb_dat_i;

                            when ADDRESS_REGISTER_DATA_B =>
                                register_data_operand_b <= wb_dat_i;

                            when ADDRESS_REGISTER_DMA_SOURCE_A =>
                                register_dma_source_address_a <= wb_dat_i;

                            when ADDRESS_REGISTER_DMA_SOURCE_B =>
                                register_dma_source_address_b <= wb_dat_i;

                            when ADDRESS_REGISTER_DMA_LENGTH =>
                                register_dma_transfer_length <= wb_dat_i;

                            when ADDRESS_REGISTER_STATUS =>
                                -- Mécanisme W1C (Write-1-to-Clear) sur l'interruption
                                if wb_dat_i(2) = '1' then
                                    interrupt_request_internal_register <= '0';
                                end if;

                            when others =>
                                null;
                        end case;
                    else
                        -- ==================== LECTURE CSR ====================
                        case wb_adr_i is
                            when ADDRESS_REGISTER_CONTROL =>
                                wb_dat_o <= (2 => register_control_irq_enable, others => '0');

                            when ADDRESS_REGISTER_STATUS =>
                                wb_dat_o <= (
                                    0 => is_busy_programmed_io,
                                    1 => flag_done_programmed_io,
                                    2 => interrupt_request_internal_register,
                                    3 => dma_busy_flag,
                                    4 => flag_done_dma_transfer,
                                    others => '0'
                                );

                            when ADDRESS_REGISTER_DATA_A =>
                                wb_dat_o <= register_data_operand_a;

                            when ADDRESS_REGISTER_DATA_B =>
                                wb_dat_o <= register_data_operand_b;

                            when ADDRESS_REGISTER_ACCUMULATOR_OUT =>
                                wb_dat_o <= std_logic_vector(mac_accumulator_output_signal);

                            when ADDRESS_REGISTER_DMA_SOURCE_A =>
                                wb_dat_o <= register_dma_source_address_a;

                            when ADDRESS_REGISTER_DMA_SOURCE_B =>
                                wb_dat_o <= register_dma_source_address_b;

                            when ADDRESS_REGISTER_DMA_LENGTH =>
                                wb_dat_o <= register_dma_transfer_length;

                            when others =>
                                wb_dat_o <= (others => '0');
                        end case;
                    end if;
                end if;
            end if;
        end if;
    end process process_wishbone_slave_bus_registers;

end architecture rtl;