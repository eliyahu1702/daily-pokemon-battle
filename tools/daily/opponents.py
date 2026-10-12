# Daily opponents that aren't monotype gym leaders or Elite Four members (see leaders.py).
#
# SIGNATURE: champions, villains, rivals and Frontier Brains with teams built from their
# canon rosters. Each gets `variants` teams: the first is their classic six, the others swap
# in the rest of the pool and alternate Smogon sets, so the same character can show up with
# a different look.
#
# Fields: (key, name, class, pic, theme, variants, pool). The first species in `pool` is the
# ace and is always on the team. `theme` is the line shown under the opponent's name before
# the battle (keep it under ~32 characters).
#
# COMPETITIVE: Battle Tower aces running real competitive archetypes (rain, sun, stall,
# hyper offense...). Fields: (key, name, class, pic, theme, team of six). A species written
# "Name:Move" gets a set with that move.

SIGNATURE = [
    # Champions
    ("RED",       "RED",       "Pkmn Trainer 1", "Red",                  "The legend of Mt. Silver.", 2,
     ["Pikachu", "Charizard", "Venusaur", "Blastoise", "Snorlax", "Lapras", "Espeon"]),
    ("BLUE",      "BLUE",      "Champion",       "Champion Rival FRLG",  "Kanto's first Champion.", 2,
     ["Pidgeot", "Alakazam", "Rhyperior", "Exeggutor", "Arcanine", "Gyarados", "Machamp"]),
    ("LANCE",     "LANCE",     "Champion",       "Elite Four Lance FRLG", "Bringing his dragon squad.", 2,
     ["Dragonite", "Gyarados", "Aerodactyl", "Charizard", "Kingdra", "Salamence", "Garchomp"]),
    ("STEVEN",    "STEVEN",    "Champion",       "Steven",               "Bringing his signature team.", 2,
     ["Metagross", "Skarmory", "Claydol", "Aggron", "Cradily", "Armaldo", "Excadrill"]),
    ("WALLACE",   "WALLACE",   "Champion",       "Champion Wallace",     "Bringing his signature team.", 2,
     ["Milotic", "Gyarados", "Ludicolo", "Tentacruel", "Whiscash", "Swampert", "Starmie"]),
    ("CYNTHIA",   "CYNTHIA",   "Champion",       "Expert F",             "Bringing her signature team.", 2,
     ["Garchomp", "Spiritomb", "Roserade", "Togekiss", "Lucario", "Milotic", "Gastrodon", "Glaceon"]),
    ("ALDER",     "ALDER",     "Champion",       "Expert M",             "Bringing his signature team.", 2,
     ["Volcarona", "Bouffalant", "Accelgor", "Druddigon", "Vanilluxe", "Escavalier", "Krookodile"]),
    ("IRIS",      "IRIS",      "Champion",       "Dragon Tamer",         "Bringing her signature team.", 2,
     ["Haxorus", "Hydreigon", "Archeops", "Aggron", "Lapras", "Druddigon", "Salamence"]),
    ("DIANTHA",   "DIANTHA",   "Champion",       "Beauty",               "Bringing her signature team.", 2,
     ["Gardevoir", "Hawlucha", "Tyrantrum", "Aurorus", "Goodra", "Gourgeist"]),
    ("KUKUI",     "KUKUI",     "Champion",       "Pokemon Ranger M",     "Bringing his signature team.", 2,
     ["Incineroar", "Lycanroc", "Ninetales-Alola", "Braviary", "Snorlax", "Magnezone", "Primarina"]),
    ("LEON",      "LEON",      "Champion",       "Cooltrainer M",        "The unbeatable Champion.", 2,
     ["Charizard", "Dragapult", "Aegislash", "Haxorus", "Rhyperior", "Seismitoad", "Mr. Rime", "Cinderace"]),
    ("GEETA",     "GEETA",     "Champion",       "Lady",                 "Paldea's Top Champion.", 2,
     ["Glimmora", "Kingambit", "Gogoat", "Veluza", "Avalugg", "Espathra"]),
    ("NEMONA",    "NEMONA",    "Champion",       "Cooltrainer F",        "Your rival is fired up!", 2,
     ["Pawmot", "Lycanroc", "Goodra", "Dudunsparce", "Orthworm", "Meowscarada", "Quaquaval"]),
    ("KIERAN",    "KIERAN",    "Champion",       "School Kid M",         "The Blueberry League Champ.", 2,
     ["Hydrapple", "Kingambit", "Incineroar", "Dragonite", "Sinistcha", "Ogerpon"]),
    # Villains
    ("GIOVANNI",  "GIOVANNI",  "Boss FRLG",      "Leader Giovanni FRLG", "Team Rocket's boss.", 2,
     ["Nidoking", "Persian", "Nidoqueen", "Rhyperior", "Kangaskhan", "Dugtrio", "Honchkrow", "Garchomp"]),
    ("ARCHIE",    "ARCHIE",    "Aqua Leader",    "Aqua Leader Archie",   "Team Aqua's leader.", 2,
     ["Sharpedo", "Crobat", "Mightyena", "Muk", "Kingdra", "Pelipper", "Toxapex"]),
    ("MAXIE",     "MAXIE",     "Magma Leader",   "Magma Leader Maxie",   "Team Magma's leader.", 2,
     ["Camerupt", "Crobat", "Mightyena", "Weezing", "Torkoal", "Houndoom", "Hippowdon"]),
    ("CYRUS",     "CYRUS",     "Boss FRLG",      "Gentleman",            "Team Galactic's boss.", 2,
     ["Weavile", "Honchkrow", "Crobat", "Gyarados", "Houndoom", "Electivire"]),
    ("GHETSIS",   "GHETSIS",   "Boss FRLG",      "Gentleman FRLG",       "Team Plasma's true leader.", 2,
     ["Hydreigon", "Cofagrigus", "Seismitoad", "Eelektross", "Bisharp", "Toxicroak", "Drapion"]),
    ("N",         "N",         "Pkmn Trainer 1", "Rich Boy",             "The King of Team Plasma.", 2,
     ["Zoroark", "Carracosta", "Vanilluxe", "Archeops", "Klinklang", "Zebstrika", "Volcarona"]),
    ("LYSANDRE",  "LYSANDRE",  "Boss FRLG",      "Gentleman",            "Team Flare's boss.", 2,
     ["Pyroar", "Mienshao", "Honchkrow", "Gyarados", "Tyranitar", "Talonflame"]),
    ("GUZMA",     "GUZMA",     "Boss FRLG",      "Biker FRLG",           "Team Skull's big bad boss.", 2,
     ["Golisopod", "Ariados", "Masquerain", "Pinsir", "Scizor", "Vikavolt"]),
    ("LUSAMINE",  "LUSAMINE",  "Boss FRLG",      "Lady",                 "The Aether Foundation head.", 2,
     ["Clefable", "Lilligant", "Mismagius", "Milotic", "Bewear", "Pheromosa"]),
    ("ROSE",      "ROSE",      "Boss FRLG",      "Gentleman",            "Chairman of Macro Cosmos.", 2,
     ["Copperajah", "Escavalier", "Ferrothorn", "Perrserker", "Klinklang", "Duraludon"]),
    ("VOLO",      "VOLO",      "Champion",       "Expert M",             "The wandering merchant...?", 2,
     ["Spiritomb", "Garchomp", "Roserade", "Togekiss", "Lucario", "Arcanine-Hisui"]),
    # Rivals and other strong trainers
    ("SILVER",    "SILVER",    "Rival",          "Rival Late FRLG",      "Your bitter Johto rival.", 2,
     ["Feraligatr", "Crobat", "Weavile", "Gengar", "Alakazam", "Magnezone", "Typhlosion"]),
    ("WALLY",     "WALLY",     "Rival",          "Wally",                "Wally has grown strong.", 2,
     ["Gallade", "Altaria", "Roserade", "Magnezone", "Flygon", "Gardevoir"]),
    ("HOP",       "HOP",       "Rival",          "Youngster",            "Leon's little brother.", 2,
     ["Rillaboom", "Dubwool", "Corviknight", "Snorlax", "Pincurchin", "Cramorant"]),
    ("ARVEN",     "ARVEN",     "Rival",          "Camper",               "Paldea's sandwich master.", 2,
     ["Mabosstiff", "Garganacl", "Scovillain", "Toedscruel", "Tinkaton", "Skeledirge"]),
    ("PENNY",     "PENNY",     "Pkmn Trainer 1", "Picnicker",            "Eevee lover and hacker.", 2,
     ["Sylveon", "Vaporeon", "Jolteon", "Flareon", "Umbreon", "Leafeon", "Glaceon", "Espeon"]),
    # Frontier Brains
    ("PALMER",    "PALMER",    "Expert",         "Expert M",             "The Battle Tower Tycoon.", 2,
     ["Rhyperior", "Milotic", "Dragonite", "Heatran", "Regigigas", "Cresselia"]),
    ("ANABEL",    "ANABEL",    "Salon Maiden",   "Salon Maiden Anabel",  "The Battle Tower's maiden.", 2,
     ["Latios", "Raikou", "Snorlax", "Alakazam", "Entei", "Metagross"]),
    ("TUCKER",    "TUCKER",    "Dome Ace",       "Dome Ace Tucker",      "The Battle Dome's ace.", 2,
     ["Salamence", "Metagross", "Latias", "Swampert", "Charizard", "Arcanine"]),
    ("SPENSER",   "SPENSER",   "Palace Maven",   "Palace Maven Spenser", "The Battle Palace's maven.", 2,
     ["Suicune", "Crobat", "Slaking", "Lapras", "Arcanine", "Gengar"]),
    ("GRETA",     "GRETA",     "Arena Tycoon",   "Arena Tycoon Greta",   "The Battle Arena's tycoon.", 2,
     ["Heracross", "Umbreon", "Gengar", "Breloom", "Machamp", "Shedinja"]),
    ("LUCY",      "LUCY",      "Pike Queen",     "Pike Queen Lucy",      "The Battle Pike's queen.", 2,
     ["Milotic", "Seviper", "Shuckle", "Gyarados", "Steelix", "Arbok"]),
    ("BRANDON",   "BRANDON",   "Pyramid King",   "Pyramid King Brandon", "The Battle Pyramid's king.", 2,
     ["Regirock", "Registeel", "Regice", "Dragonite", "Arcanine", "Metagross"]),
]

COMPETITIVE = [
    ("ACE_RAIN",     "MARINA",  "Cooltrainer",   "Cooltrainer F",  "Tower ace: rain team.",
     ["Pelipper", "Barraskewda", "Archaludon", "Kingdra", "Zapdos", "Ferrothorn"]),
    ("ACE_SUN",      "SOL",     "Cooltrainer",   "Cooltrainer M",  "Tower ace: sun team.",
     ["Torkoal", "Venusaur", "Walking Wake", "Great Tusk", "Gholdengo", "Ninetales"]),
    ("ACE_SAND",     "DUNE",    "Expert",        "Expert M",       "Tower ace: sand team.",
     ["Tyranitar", "Excadrill", "Garchomp", "Landorus-Therian", "Corviknight", "Rotom-Wash"]),
    ("ACE_SNOW",     "HAIL",    "Cooltrainer",   "Cooltrainer F",  "Tower ace: snow team.",
     ["Ninetales-Alola", "Baxcalibur", "Iron Bundle", "Kingambit", "Garganacl", "Weavile"]),
    ("ACE_HO",       "BLITZ",   "Cooltrainer",   "Cooltrainer M",  "Tower ace: hyper offense.",
     ["Glimmora", "Kingambit", "Dragapult", "Iron Valiant", "Great Tusk", "Gholdengo"]),
    ("ACE_STALL",    "WALL",    "Expert",        "Expert F",       "Tower ace: full stall.",
     ["Blissey", "Toxapex", "Dondozo", "Gliscor", "Corviknight", "Clodsire"]),
    ("ACE_BALANCE",  "SAGE",    "Expert",        "Expert M",       "Tower ace: balance team.",
     ["Landorus-Therian", "Gholdengo", "Toxapex", "Dragonite", "Iron Valiant", "Heatran"]),
    ("ACE_TR",       "ROOM",    "Psychic",       "Psychic F",      "Tower ace: Trick Room.",
     ["Hatterene:Trick Room", "Ursaluna", "Torkoal", "Iron Hands", "Dusclops:Trick Room", "Stakataka:Trick Room"]),
    ("ACE_VOLTTURN", "PIVOT",   "Cooltrainer",   "Cooltrainer M",  "Tower ace: VoltTurn.",
     ["Rotom-Wash", "Landorus-Therian", "Scizor", "Iron Valiant", "Zapdos", "Heatran"]),
    ("ACE_SETUP",    "DRAKE",   "Dragon Tamer",  "Dragon Tamer",   "Tower ace: setup sweepers.",
     ["Dragonite", "Dragapult", "Kingambit", "Volcarona", "Garchomp", "Gholdengo"]),
    ("ACE_DPP",      "CLASSIC", "Expert",        "Expert F",       "Tower ace: DPP classic.",
     ["Tyranitar", "Gengar", "Metagross", "Heatran", "Gyarados", "Breloom"]),
    ("ACE_BWSAND",   "TERRA",   "Expert",        "Expert M",       "Tower ace: BW sand.",
     ["Tyranitar", "Excadrill", "Landorus", "Ferrothorn", "Latios", "Starmie"]),
]

# Hand-written sets for signature Pokémon with no Smogon set in the formats we keep.
CUSTOM_SETS = {
    "Pikachu": {"item": "Light Ball", "ability": "Lightning Rod", "nature": "Hasty",
                "evs": {"atk": 252, "spa": 4, "spe": 252},
                "moves": ["Volt Tackle", "Surf", "Iron Tail", "Fake Out"]},
}
