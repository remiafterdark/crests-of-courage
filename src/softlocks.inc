

SOFTLOCK("R-CHY9", "Midna riding before the prison scenes: nobody breaks the chain",
    "norando; on 0x0C10; off 0x0540; off 0x0520",
    "clear 0x0C10", "")

SOFTLOCK("R-R5NW", "on Zelda's tower with the first warp already set: no way off the roof",
    "norando; stage R_SP107; room 3; notpoint 20; on 0x0502; nottransform 3",
    "clear 0x0502; swclear 1:30", "Walk up to Zelda again.")

SOFTLOCK("R-TF3K", "tower switch 0x1E on, Zelda never met: she only says hello",
    "norando; stage R_SP107; notpoint 20; off 0x0508; swon 1:30",
    "swclear 1:30", "")

SOFTLOCK("R-YLAL", "Faron's shadow beasts beaten (sw 71) without their intro (sw 14): Midna stops you forever",
    "swon 2:71; swoff 2:14",
    "swset 2:14", "")

SOFTLOCK("R-BAWJ", "graveyard switch 102 without Rutela's armor (M_037): her layer with no Rutela",
    "norando; off 0x0804; swon 3:102",
    "set 0x0804", "Leave the graveyard and come back in.")
SOFTLOCK("R-BAWJ", "rando: graveyard switch 102 without 101 or her check: her layer with no Rutela",
    "rando; on 0x0810; off 0x0804; swon 3:102; swoff 3:101",
    "swset 3:101; swclear 3:102", "Leave the graveyard and come back in.")

SOFTLOCK("R-Q64X", "Midna in the shadows (F_0800) outside Ordon: no Midna button",
    "on 0x6140; notstage F_SP103,F_SP104,F_SP00,R_SP01",
    "clear 0x6140", "She can be called again.")

SOFTLOCK("2026-09-29", "Epona tamed (M_023) before Faron is cleared: Ordon in the wrong layer, nobody there",
    "norando; on 0x0601; notdarkclear 0",
    "clear 0x0601", "")

SOFTLOCK("R-8AJB", "still the wolf after Faron's spirit, no way to change back",
    "norando; wolf; notwilight; on 0x0610; off 0x0602; off 0x0D04",
    "human", "Midna changed you back.")

SOFTLOCK("R-CRB8", "Bo's boots flag without the Iron Boots: his talk loops",
    "norando; on 0x1C20; noitem 0x45",
    "clear 0x1C20", "Talk to Bo again.")

SOFTLOCK("R-EXSQ", "randomizer: Bo stops you in his hallway forever after a partner won the sumo",
    "rando; stage R_SP01; on 0x3901; off 0x1210; tbon 0:2",
    "set 0x1210", "")
