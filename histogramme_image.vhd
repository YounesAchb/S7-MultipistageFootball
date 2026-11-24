library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Histogramme d'une image de luminance sur 8 bits.
-- Tous les signaux d'entree sont synchrones au meme clk.
-- debut_image et fin_image accompagnent les pixels valides aux extremites.
entity histogramme_image is
    generic (
        BITS_COMPTEUR : positive := 24  -- Assez pour une image UHD/4K.
    );
    port (
        clk             : in  std_logic;
        reset           : in  std_logic;  -- Synchrone, actif a 1.
        pixel           : in  unsigned(7 downto 0);
        pixel_valide    : in  std_logic;
        debut_image     : in  std_logic;
        fin_image       : in  std_logic;

        adresse_histo   : in  unsigned(7 downto 0);
        effectif_histo  : out unsigned(BITS_COMPTEUR - 1 downto 0);
        image_terminee  : out std_logic
    );
end entity histogramme_image;

architecture rtl of histogramme_image is
    subtype compteur_t is unsigned(BITS_COMPTEUR - 1 downto 0);
    type histogramme_t is array (0 to 255) of compteur_t;
    constant HISTO_VIDE : histogramme_t := (others => (others => '0'));

    signal histo_travail : histogramme_t;
    signal histo_resultat : histogramme_t;
    signal acquisition_en_cours : std_logic;
begin
    -- Le lecteur choisit une case de 0 a 255 avec adresse_histo.
    -- Le tableau publie reste disponible jusqu'a la fin de l'image suivante.
    effectif_histo <= histo_resultat(to_integer(adresse_histo));

    process (clk)
        variable niveau : natural range 0 to 255;
        variable histo_suivant : histogramme_t;
    begin
        if rising_edge(clk) then
            image_terminee <= '0';

            if reset = '1' then
                histo_travail <= HISTO_VIDE;
                histo_resultat <= HISTO_VIDE;
                acquisition_en_cours <= '0';

            elsif pixel_valide = '1' and
                  (debut_image = '1' or acquisition_en_cours = '1') then
                niveau := to_integer(pixel);

                if debut_image = '1' then
                    histo_suivant := HISTO_VIDE;
                else
                    histo_suivant := histo_travail;
                end if;

                -- Un pixel valide ajoute exactement 1 dans sa case.
                -- BITS_COMPTEUR doit pouvoir contenir le total de pixels.
                histo_suivant(niveau) := histo_suivant(niveau) + 1;
                histo_travail <= histo_suivant;

                if fin_image = '1' then
                    -- Le tableau a deja pris en compte le dernier pixel.
                    histo_resultat <= histo_suivant;
                    image_terminee <= '1';
                    acquisition_en_cours <= '0';
                else
                    acquisition_en_cours <= '1';
                end if;
            end if;
        end if;
    end process;
end architecture rtl;
