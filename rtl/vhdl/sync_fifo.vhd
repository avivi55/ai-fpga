library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sync_fifo is
    generic (
        DATA_BIT_WIDTH : positive := 32;
        FIFO_DEPTH_WORDS : positive := 16
    );
    port (
        clock : in std_logic;
        reset_active_low : in std_logic;

        write_enable_input : in std_logic;
        data_write_input : in std_logic_vector(DATA_BIT_WIDTH - 1 downto 0);
        fifo_full_flag_output : out std_logic;

        read_enable_input : in std_logic;
        data_read_output : out std_logic_vector(DATA_BIT_WIDTH - 1 downto 0);
        fifo_empty_flag_output : out std_logic;

        occupancy_counter_output : out unsigned(4 downto 0)
    );
end entity sync_fifo;

architecture rtl of sync_fifo is

    type memory_array_type is array (0 to FIFO_DEPTH_WORDS - 1) of std_logic_vector(DATA_BIT_WIDTH - 1 downto 0);
    signal distributed_ram_memory : memory_array_type := (others => (others => '0'));

    signal write_memory_pointer : unsigned(3 downto 0) := (others => '0');
    signal read_memory_pointer : unsigned(3 downto 0) := (others => '0');
    signal occupancy_counter : unsigned(4 downto 0) := (others => '0');

    signal is_fifo_empty : std_logic;
    signal is_fifo_full : std_logic;

begin

    is_fifo_empty <= '1' when (occupancy_counter = 0) else
        '0';
    is_fifo_full <= '1' when (occupancy_counter = FIFO_DEPTH_WORDS) else
        '0';

    fifo_empty_flag_output <= is_fifo_empty;
    fifo_full_flag_output <= is_fifo_full;
    occupancy_counter_output <= occupancy_counter;

    data_read_output <= distributed_ram_memory(to_integer(read_memory_pointer));

    process_synchronous_fifo_write_read_counter : process (clock, reset_active_low)
    begin
        if reset_active_low = '0' then
            write_memory_pointer <= (others => '0');
            read_memory_pointer <= (others => '0');
            occupancy_counter <= (others => '0');

        elsif rising_edge(clock) then
            if (write_enable_input = '1' and is_fifo_full = '0') then
                distributed_ram_memory(to_integer(write_memory_pointer)) <= data_write_input;
                write_memory_pointer <= write_memory_pointer + 1;
            end if;

            if (read_enable_input = '1' and is_fifo_empty = '0') then
                read_memory_pointer <= read_memory_pointer + 1;
            end if;

            if (write_enable_input = '1' and is_fifo_full = '0') and (read_enable_input = '0' or is_fifo_empty = '1') then
                occupancy_counter <= occupancy_counter + 1;
            elsif (read_enable_input = '1' and is_fifo_empty = '0') and (write_enable_input = '0' or is_fifo_full = '1') then
                occupancy_counter <= occupancy_counter - 1;
            end if;
        end if;
    end process process_synchronous_fifo_write_read_counter;

end architecture rtl;