----------------------------------------------------------------------------------
-- PROJET ENSEA S7 — EdgeTrack-Foot / Multipistage Football
-- Fichier : src/fpga/normalisation_ip.vhd
-- 
-- DESCRIPTION :
--   IP Core VHDL de Normalisation d'Image (Étirement de Dynamique).
--   Interface AXI4-Stream pour le flux vidéo (1 pixel par cycle d'horloge).
--   Transcodage direct par table LUT 256 octets instanciée en Block RAM (BRAM).
----------------------------------------------------------------------------------

library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity normalisation_ip is
    generic (
        LARGEUR_CANAL : integer := 8   -- 8 bits par composante (R, G, B)
    );
    port (
        -- Horloge et Reset global synchrone (actif bas)
        aclk          : in  std_logic;
        aresetn       : in  std_logic;

        -- -------------------------------------------------------------
        -- Interface Esclave AXI4-Stream : Entrée Vidéo Brute (Caméra)
        -- Format pixel : [23:16] = R, [15:8] = G, [7:0] = B
        -- -------------------------------------------------------------
        s_axis_tdata  : in  std_logic_vector(23 downto 0);
        s_axis_tvalid : in  std_logic;
        s_axis_tready : out std_logic;
        s_axis_tuser  : in  std_logic;                    -- SOF (Start of Frame)
        s_axis_tlast  : in  std_logic;                    -- EOL (End of Line)

        -- -------------------------------------------------------------
        -- Interface Maître AXI4-Stream : Sortie Vidéo Normalisée (DDR/PS)
        -- Format pixel : [23:16] = R_norm, [15:8] = G_norm, [7:0] = B_norm
        -- -------------------------------------------------------------
        m_axis_tdata  : out std_logic_vector(23 downto 0);
        m_axis_tvalid : out std_logic;
        m_axis_tready : in  std_logic;
        m_axis_tuser  : out std_logic;
        m_axis_tlast  : out std_logic;

        -- -------------------------------------------------------------
        -- Port de Configuration de la LUT (connecté aux registres AXI-Lite)
        -- Permet au processeur ARM d'écrire la table de 256 octets
        -- -------------------------------------------------------------
        lut_we        : in  std_logic;                    -- Écriture active à '1'
        lut_addr      : in  std_logic_vector(7 downto 0); -- Index [0..255]
        lut_wdata     : in  std_logic_vector(7 downto 0)  -- Valeur normalisée
    );
end entity normalisation_ip;

architecture Behavioral of normalisation_ip is

    -- Définition du type de mémoire RAM pour la Look-Up Table (256 x 8 bits)
    type ram_type is array (0 to 255) of std_logic_vector(7 downto 0);
    
    -- Table d'étirement LUT partagée (synthétisée en Distributed RAM ou 1/2 Block RAM)
    signal lut_mem : ram_type := (others => (others => '0'));

    -- Décomposition des composantes couleur d'entrée
    signal in_r, in_g, in_b : std_logic_vector(7 downto 0);

    -- Signaux de contrôle du pipeline
    signal pipeline_en : std_logic;

begin

    -- Extraction des canaux couleur sur le bus AXI-Stream d'entrée
    in_r <= s_axis_tdata(23 downto 16);
    in_g <= s_axis_tdata(15 downto 8);
    in_b <= s_axis_tdata(7 downto 0);

    -- Gestion du Handshake AXI-Stream (écoulement du flux sans blocage)
    pipeline_en   <= s_axis_tvalid and (m_axis_tready or not s_axis_tvalid);
    s_axis_tready <= m_axis_tready;

    -- -------------------------------------------------------------
    -- Process 1 : Écriture de la LUT par le processeur ARM (AXI-Lite)
    -- -------------------------------------------------------------
    process(aclk)
    begin
        if rising_edge(aclk) then
            if lut_we = '1' then
                lut_mem(to_integer(unsigned(lut_addr))) <= lut_wdata;
            end if;
        end if;
    end process;

    -- -------------------------------------------------------------
    -- Process 2 : Pipeline de Traitement Pixel au fil de l'eau
    -- Débit garanti : 1 pixel traité par coup d'horloge (II = 1)
    -- -------------------------------------------------------------
    process(aclk)
    begin
        if rising_edge(aclk) then
            if aresetn = '0' then
                m_axis_tvalid <= '0';
                m_axis_tdata  <= (others => '0');
                m_axis_tuser  <= '0';
                m_axis_tlast  <= '0';
            elsif pipeline_en = '1' then
                m_axis_tvalid <= s_axis_tvalid;
                m_axis_tuser  <= s_axis_tuser;
                m_axis_tlast  <= s_axis_tlast;

                if s_axis_tvalid = '1' then
                    -- Normalisation instantanée par lecture de la LUT
                    m_axis_tdata(23 downto 16) <= lut_mem(to_integer(unsigned(in_r)));
                    m_axis_tdata(15 downto 8)  <= lut_mem(to_integer(unsigned(in_g)));
                    m_axis_tdata(7 downto 0)   <= lut_mem(to_integer(unsigned(in_b)));
                end if;
            end if;
        end if;
    end process;

end architecture Behavioral;
