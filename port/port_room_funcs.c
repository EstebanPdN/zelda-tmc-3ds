/* AUTO-GENERATED */
/* Maps (area, room) -> function pointers for room properties 4-7. */
/* On GBA, these are stored as Thumb code addresses in ROM data. */
/* On 64-bit PC, we must provide native function pointers instead. */

#ifdef PC_PORT

#include "global.h"
#include <stddef.h>

/* Forward declarations for all room init functions */
extern void sub_StateChange_37_0(void);
extern void sub_StateChange_37_1(void);
extern void sub_StateChange_45_Main(void);
extern void sub_StateChange_47_0(void);
extern void sub_StateChange_47_1(void);
extern void sub_StateChange_47_2(void);
extern void sub_StateChange_47_3(void);
extern void sub_StateChange_47_4(void);
extern void sub_StateChange_4D_Main(void);
extern void sub_StateChange_57_Main(void);
extern void sub_StateChange_5F_Main(void);
extern void sub_StateChange_67_Main(void);
extern void sub_StateChange_6F_Main(void);
extern void sub_StateChange_77_Main(void);
extern void sub_StateChange_7F_Main(void);
extern void sub_StateChange_87_Main(void);
extern void sub_StateChange_8F_Main(void);
extern void sub_StateChange_ArmosInteriors_6(void);
extern void sub_StateChange_ArmosInteriors_8(void);
extern void sub_StateChange_ArmosInteriors_FortressOfWindsLeft(void);
extern void sub_StateChange_ArmosInteriors_FortressOfWindsRight(void);
extern void sub_StateChange_ArmosInteriors_RuinsEntranceNorth(void);
extern void sub_StateChange_ArmosInteriors_RuinsEntranceSouth(void);
extern void sub_StateChange_ArmosInteriors_RuinsGrassPath(void);
extern void sub_StateChange_ArmosInteriors_RuinsLeft(void);
extern void sub_StateChange_ArmosInteriors_RuinsMiddleLeft(void);
extern void sub_StateChange_ArmosInteriors_RuinsMiddleRight(void);
extern void sub_StateChange_ArmosInteriors_RuinsRight(void);
extern void sub_StateChange_Beanstalks_EasternHills(void);
extern void sub_StateChange_Beanstalks_EasternHillsClimb(void);
extern void sub_StateChange_Beanstalks_LakeHylia(void);
extern void sub_StateChange_Beanstalks_LakeHyliaClimb(void);
extern void sub_StateChange_Beanstalks_MountCrenel(void);
extern void sub_StateChange_Beanstalks_MountCrenelClimb(void);
extern void sub_StateChange_Beanstalks_Ruins(void);
extern void sub_StateChange_Beanstalks_RuinsClimb(void);
extern void sub_StateChange_Beanstalks_WesternWoods(void);
extern void sub_StateChange_Beanstalks_WesternWoodsClimb(void);
extern void sub_StateChange_CastleGardenMinishHoles_East(void);
extern void sub_StateChange_CastleGardenMinishHoles_West(void);
extern void sub_StateChange_CastleGarden_Main(void);
extern void sub_StateChange_CastorCaves_Darknut(void);
extern void sub_StateChange_CastorCaves_HeartPiece(void);
extern void sub_StateChange_CastorCaves_North(void);
extern void sub_StateChange_CastorCaves_South(void);
extern void sub_StateChange_CastorCaves_WindRuins(void);
extern void sub_StateChange_CastorDarknut_Hall(void);
extern void sub_StateChange_CastorDarknut_Main(void);
extern void sub_StateChange_CastorWilds_Main(void);
extern void sub_StateChange_CaveOfFlamesBoss_Main(void);
extern void sub_StateChange_CaveOfFlames_18(void);
extern void sub_StateChange_CaveOfFlames_AfterCane(void);
extern void sub_StateChange_CaveOfFlames_BeforeGleerok(void);
extern void sub_StateChange_CaveOfFlames_Bobomb(void);
extern void sub_StateChange_CaveOfFlames_BossDoor(void);
extern void sub_StateChange_CaveOfFlames_CartToSpinyChus(void);
extern void sub_StateChange_CaveOfFlames_CartWest(void);
extern void sub_StateChange_CaveOfFlames_Compass(void);
extern void sub_StateChange_CaveOfFlames_Entrance(void);
extern void sub_StateChange_CaveOfFlames_Helmasaur(void);
extern void sub_StateChange_CaveOfFlames_MainCart(void);
extern void sub_StateChange_CaveOfFlames_MinishLava(void);
extern void sub_StateChange_CaveOfFlames_MinishSpikes(void);
extern void sub_StateChange_CaveOfFlames_NorthEntrance(void);
extern void sub_StateChange_CaveOfFlames_PathBossKey(void);
extern void sub_StateChange_CaveOfFlames_PathBossKey2(void);
extern void sub_StateChange_CaveOfFlames_Rollobite(void);
extern void sub_StateChange_CaveOfFlames_RollobiteSwitch(void);
extern void sub_StateChange_CaveOfFlames_SpinyChus(void);
extern void sub_StateChange_Caves_2(void);
extern void sub_StateChange_Caves_3(void);
extern void sub_StateChange_Caves_4(void);
extern void sub_StateChange_Caves_5(void);
extern void sub_StateChange_Caves_6(void);
extern void sub_StateChange_Caves_A(void);
extern void sub_StateChange_Caves_Boomerang(void);
extern void sub_StateChange_Caves_BottleBusinessScrub(void);
extern void sub_StateChange_Caves_HeartPieceHallway(void);
extern void sub_StateChange_Caves_HillsKeeseChest(void);
extern void sub_StateChange_Caves_HyruleTownWaterfall(void);
extern void sub_StateChange_Caves_KinstoneBusinessScrub(void);
extern void sub_StateChange_Caves_LonLonRanch(void);
extern void sub_StateChange_Caves_LonLonRanchSecret(void);
extern void sub_StateChange_Caves_LonLonRanchWallet(void);
extern void sub_StateChange_Caves_NorthHyruleFieldFairyFountain(void);
extern void sub_StateChange_Caves_SouthHyruleFieldFairyFountain(void);
extern void sub_StateChange_Caves_SouthHyruleFieldRupee(void);
extern void sub_StateChange_Caves_ToGraveyard(void);
extern void sub_StateChange_Caves_TrilbyFairyFountain(void);
extern void sub_StateChange_Caves_TrilbyHighlands(void);
extern void sub_StateChange_Caves_TrilbyKeeseChest(void);
extern void sub_StateChange_Caves_TrilbyMittsFairyFountain(void);
extern void sub_StateChange_Caves_TrilbyRupee(void);
extern void sub_StateChange_CloudTops_Bottom(void);
extern void sub_StateChange_CloudTops_House(void);
extern void sub_StateChange_CloudTops_Middle(void);
extern void sub_StateChange_CrenelCaves_BlockPushing(void);
extern void sub_StateChange_CrenelCaves_BombBusinessScrub(void);
extern void sub_StateChange_CrenelCaves_BridgeSwitch(void);
extern void sub_StateChange_CrenelCaves_ChuchuPotChest(void);
extern void sub_StateChange_CrenelCaves_ExitToMines(void);
extern void sub_StateChange_CrenelCaves_FairyFountain(void);
extern void sub_StateChange_CrenelCaves_GripRing(void);
extern void sub_StateChange_CrenelCaves_HelmasaurHallway(void);
extern void sub_StateChange_CrenelCaves_Hermit(void);
extern void sub_StateChange_CrenelCaves_HintScrub(void);
extern void sub_StateChange_CrenelCaves_LadderToSpringWater(void);
extern void sub_StateChange_CrenelCaves_MushroomKeese(void);
extern void sub_StateChange_CrenelCaves_PillarCave(void);
extern void sub_StateChange_CrenelCaves_RupeeFairyFountain(void);
extern void sub_StateChange_CrenelCaves_SpinyChuPuzzle(void);
extern void sub_StateChange_CrenelCaves_ToGrayblade(void);
extern void sub_StateChange_CrenelCaves_WaterHeartPiece(void);
extern void sub_StateChange_CrenelMinishPaths_CrenelBean(void);
extern void sub_StateChange_CrenelMinishPaths_CrenelWater(void);
extern void sub_StateChange_CrenelMinishPaths_MelarisMine(void);
extern void sub_StateChange_CrenelMinishPaths_Rainfall(void);
extern void sub_StateChange_DarkHyruleCastleBridge_Main(void);
extern void sub_StateChange_DarkHyruleCastleOutside_Garden(void);
extern void sub_StateChange_DarkHyruleCastleOutside_ZeldaStatuePlatform(void);
extern void sub_StateChange_DarkHyruleCastle_1FEntrance(void);
extern void sub_StateChange_DarkHyruleCastle_1FThroneRoom(void);
extern void sub_StateChange_DarkHyruleCastle_2FBottomLeftDarknuts(void);
extern void sub_StateChange_DarkHyruleCastle_2FBottomRightDarknut(void);
extern void sub_StateChange_DarkHyruleCastle_2FTopLeftDarknut(void);
extern void sub_StateChange_DarkHyruleCastle_2FTopRightDarknuts(void);
extern void sub_StateChange_DarkHyruleCastle_3FTripleDarknut(void);
extern void sub_StateChange_DarkHyruleCastle_B1Entrance(void);
extern void sub_StateChange_DarkHyruleCastle_B1Left(void);
extern void sub_StateChange_DarkHyruleCastle_B1Map(void);
extern void sub_StateChange_DarkHyruleCastle_B2Prison(void);
extern void sub_StateChange_DeepwoodShrineBoss_Main(void);
extern void sub_StateChange_DeepwoodShrineEntry_Main(void);
extern void sub_StateChange_DeepwoodShrine_13(void);
extern void sub_StateChange_DeepwoodShrine_Barrel(void);
extern void sub_StateChange_DeepwoodShrine_BluePortal(void);
extern void sub_StateChange_DeepwoodShrine_BossKey(void);
extern void sub_StateChange_DeepwoodShrine_Button(void);
extern void sub_StateChange_DeepwoodShrine_Compass(void);
extern void sub_StateChange_DeepwoodShrine_DoubleStatue(void);
extern void sub_StateChange_DeepwoodShrine_Entrance(void);
extern void sub_StateChange_DeepwoodShrine_InsideBarrel(void);
extern void sub_StateChange_DeepwoodShrine_Lever(void);
extern void sub_StateChange_DeepwoodShrine_LilyPadEast(void);
extern void sub_StateChange_DeepwoodShrine_LilyPadWest(void);
extern void sub_StateChange_DeepwoodShrine_Madderpillar(void);
extern void sub_StateChange_DeepwoodShrine_Map(void);
extern void sub_StateChange_DeepwoodShrine_Mulldozer(void);
extern void sub_StateChange_DeepwoodShrine_Pillars(void);
extern void sub_StateChange_DeepwoodShrine_PotBridge(void);
extern void sub_StateChange_DeepwoodShrine_PreBoss(void);
extern void sub_StateChange_DeepwoodShrine_StairsToB1(void);
extern void sub_StateChange_DeepwoodShrine_Torch(void);
extern void sub_StateChange_Dojos_7(void);
extern void sub_StateChange_Dojos_70(void);
extern void sub_StateChange_Dojos_71(void);
extern void sub_StateChange_Dojos_72(void);
extern void sub_StateChange_Dojos_73(void);
extern void sub_StateChange_Dojos_74(void);
extern void sub_StateChange_Dojos_75(void);
extern void sub_StateChange_Dojos_76(void);
extern void sub_StateChange_Dojos_77(void);
extern void sub_StateChange_Dojos_78(void);
extern void sub_StateChange_Dojos_79(void);
extern void sub_StateChange_Dojos_8(void);
extern void sub_StateChange_Dojos_80(void);
extern void sub_StateChange_Dojos_81(void);
extern void sub_StateChange_Dojos_82(void);
extern void sub_StateChange_Dojos_83(void);
extern void sub_StateChange_Dojos_84(void);
extern void sub_StateChange_Dojos_85(void);
extern void sub_StateChange_Dojos_86(void);
extern void sub_StateChange_Dojos_87(void);
extern void sub_StateChange_Dojos_88(void);
extern void sub_StateChange_Dojos_89(void);
extern void sub_StateChange_Dojos_9(void);
extern void sub_StateChange_Dojos_90(void);
extern void sub_StateChange_Dojos_91(void);
extern void sub_StateChange_Dojos_92(void);
extern void sub_StateChange_Dojos_93(void);
extern void sub_StateChange_Dojos_94(void);
extern void sub_StateChange_Dojos_95(void);
extern void sub_StateChange_Dojos_96(void);
extern void sub_StateChange_Dojos_97(void);
extern void sub_StateChange_Dojos_98(void);
extern void sub_StateChange_Dojos_99(void);
extern void sub_StateChange_Dojos_Grayblade(void);
extern void sub_StateChange_Dojos_Greatblade(void);
extern void sub_StateChange_Dojos_Grimblade(void);
extern void sub_StateChange_Dojos_Scarblade(void);
extern void sub_StateChange_Dojos_Splitblade(void);
extern void sub_StateChange_Dojos_Swiftblade(void);
extern void sub_StateChange_Dojos_ToGreatblade(void);
extern void sub_StateChange_Dojos_ToGreatblade0(void);
extern void sub_StateChange_Dojos_ToGreatblade1(void);
extern void sub_StateChange_Dojos_ToGreatblade2(void);
extern void sub_StateChange_Dojos_ToGreatblade3(void);
extern void sub_StateChange_Dojos_ToGreatblade4(void);
extern void sub_StateChange_Dojos_ToGreatblade5(void);
extern void sub_StateChange_Dojos_ToGreatblade6(void);
extern void sub_StateChange_Dojos_ToGreatblade7(void);
extern void sub_StateChange_Dojos_ToGreatblade8(void);
extern void sub_StateChange_Dojos_ToGreatblade9(void);
extern void sub_StateChange_Dojos_ToGrimblade(void);
extern void sub_StateChange_Dojos_ToGrimblade0(void);
extern void sub_StateChange_Dojos_ToGrimblade1(void);
extern void sub_StateChange_Dojos_ToGrimblade2(void);
extern void sub_StateChange_Dojos_ToGrimblade3(void);
extern void sub_StateChange_Dojos_ToGrimblade4(void);
extern void sub_StateChange_Dojos_ToGrimblade5(void);
extern void sub_StateChange_Dojos_ToGrimblade6(void);
extern void sub_StateChange_Dojos_ToGrimblade7(void);
extern void sub_StateChange_Dojos_ToGrimblade8(void);
extern void sub_StateChange_Dojos_ToGrimblade9(void);
extern void sub_StateChange_Dojos_ToScarblade(void);
extern void sub_StateChange_Dojos_ToScarblade0(void);
extern void sub_StateChange_Dojos_ToScarblade1(void);
extern void sub_StateChange_Dojos_ToScarblade2(void);
extern void sub_StateChange_Dojos_ToScarblade3(void);
extern void sub_StateChange_Dojos_ToScarblade4(void);
extern void sub_StateChange_Dojos_ToScarblade5(void);
extern void sub_StateChange_Dojos_ToScarblade6(void);
extern void sub_StateChange_Dojos_ToScarblade7(void);
extern void sub_StateChange_Dojos_ToScarblade8(void);
extern void sub_StateChange_Dojos_ToScarblade9(void);
extern void sub_StateChange_Dojos_ToSplitblade(void);
extern void sub_StateChange_Dojos_ToSplitblade0(void);
extern void sub_StateChange_Dojos_ToSplitblade1(void);
extern void sub_StateChange_Dojos_ToSplitblade2(void);
extern void sub_StateChange_Dojos_ToSplitblade3(void);
extern void sub_StateChange_Dojos_ToSplitblade4(void);
extern void sub_StateChange_Dojos_ToSplitblade5(void);
extern void sub_StateChange_Dojos_ToSplitblade6(void);
extern void sub_StateChange_Dojos_ToSplitblade7(void);
extern void sub_StateChange_Dojos_ToSplitblade8(void);
extern void sub_StateChange_Dojos_ToSplitblade9(void);
extern void sub_StateChange_Dojos_Waveblade(void);
extern void sub_StateChange_Dojos_Waveblade0(void);
extern void sub_StateChange_Dojos_Waveblade1(void);
extern void sub_StateChange_Dojos_Waveblade2(void);
extern void sub_StateChange_Dojos_Waveblade3(void);
extern void sub_StateChange_Dojos_Waveblade4(void);
extern void sub_StateChange_Dojos_Waveblade5(void);
extern void sub_StateChange_Dojos_Waveblade6(void);
extern void sub_StateChange_Dojos_Waveblade7(void);
extern void sub_StateChange_Dojos_Waveblade8(void);
extern void sub_StateChange_Dojos_Waveblade9(void);
extern void sub_StateChange_Empty_Main(void);
extern void sub_StateChange_EzloAuxCutscene_Main(void);
extern void sub_StateChange_FortressOfWindsTop_Main(void);
extern void sub_StateChange_FortressOfWinds_BeforeMazaal(void);
extern void sub_StateChange_FortressOfWinds_Clone(void);
extern void sub_StateChange_FortressOfWinds_Darknut(void);
extern void sub_StateChange_FortressOfWinds_EastKeyLever(void);
extern void sub_StateChange_FortressOfWinds_EyeBridge(void);
extern void sub_StateChange_FortressOfWinds_Eyegore(void);
extern void sub_StateChange_FortressOfWinds_Mazaal(void);
extern void sub_StateChange_FortressOfWinds_Pit(void);
extern void sub_StateChange_FortressOfWinds_PitPlatforms(void);
extern void sub_StateChange_FortressOfWinds_SpikeTraps(void);
extern void sub_StateChange_FortressOfWinds_Stalfos(void);
extern void sub_StateChange_FortressOfWinds_Wallmaster(void);
extern void sub_StateChange_FortressOfWinds_WestKeyLever(void);
extern void sub_StateChange_GardenFountains_East(void);
extern void sub_StateChange_GardenFountains_West(void);
extern void sub_StateChange_GoronCave_Main(void);
extern void sub_StateChange_GoronCave_Stairs(void);
extern void sub_StateChange_GoronCave_Stairs0(void);
extern void sub_StateChange_GoronCave_Stairs1(void);
extern void sub_StateChange_GoronCave_Stairs2(void);
extern void sub_StateChange_GoronCave_Stairs3(void);
extern void sub_StateChange_GoronCave_Stairs4(void);
extern void sub_StateChange_GoronCave_Stairs5(void);
extern void sub_StateChange_GoronCave_Stairs6(void);
extern void sub_StateChange_GoronCave_Stairs7(void);
extern void sub_StateChange_GoronCave_Stairs8(void);
extern void sub_StateChange_GoronCave_Stairs9(void);
extern void sub_StateChange_GreatFairies_Entrance(void);
extern void sub_StateChange_GreatFairies_Exit(void);
extern void sub_StateChange_GreatFairies_Graveyard(void);
extern void sub_StateChange_GreatFairies_MinishWoods(void);
extern void sub_StateChange_GreatFairies_MtCrenel(void);
extern void sub_StateChange_HouseInteriors1_Inn1F(void);
extern void sub_StateChange_HouseInteriors1_InnEast2F(void);
extern void sub_StateChange_HouseInteriors1_InnEastRoom(void);
extern void sub_StateChange_HouseInteriors1_InnMiddleRoom(void);
extern void sub_StateChange_HouseInteriors1_InnMinishHeartPiece(void);
extern void sub_StateChange_HouseInteriors1_InnWest2F(void);
extern void sub_StateChange_HouseInteriors1_InnWestRoom(void);
extern void sub_StateChange_HouseInteriors1_Library1F(void);
extern void sub_StateChange_HouseInteriors1_Library2F(void);
extern void sub_StateChange_HouseInteriors1_Mayor(void);
extern void sub_StateChange_HouseInteriors1_PostOffice(void);
extern void sub_StateChange_HouseInteriors1_SchoolEast(void);
extern void sub_StateChange_HouseInteriors1_SchoolWest(void);
extern void sub_StateChange_HouseInteriors2_Dampe(void);
extern void sub_StateChange_HouseInteriors2_DrLeft(void);
extern void sub_StateChange_HouseInteriors2_EastOracle(void);
extern void sub_StateChange_HouseInteriors2_Julietta(void);
extern void sub_StateChange_HouseInteriors2_LinksHouseBedroom(void);
extern void sub_StateChange_HouseInteriors2_LinksHouseEntrance(void);
extern void sub_StateChange_HouseInteriors2_LinksHouseSmith(void);
extern void sub_StateChange_HouseInteriors2_Percy(void);
extern void sub_StateChange_HouseInteriors2_Romio(void);
extern void sub_StateChange_HouseInteriors2_StockwellLakeHouse(void);
extern void sub_StateChange_HouseInteriors2_Stranger(void);
extern void sub_StateChange_HouseInteriors2_WestOracle(void);
extern void sub_StateChange_HouseInteriors3_Bakery(void);
extern void sub_StateChange_HouseInteriors3_Borlov(void);
extern void sub_StateChange_HouseInteriors3_BorlovEntrance(void);
extern void sub_StateChange_HouseInteriors3_Cafe(void);
extern void sub_StateChange_HouseInteriors3_Carlov(void);
extern void sub_StateChange_HouseInteriors3_FigurineHouse(void);
extern void sub_StateChange_HouseInteriors3_RemShoeShop(void);
extern void sub_StateChange_HouseInteriors3_Simon(void);
extern void sub_StateChange_HouseInteriors3_StockwellShop(void);
extern void sub_StateChange_HouseInteriors4_Carpenter(void);
extern void sub_StateChange_HouseInteriors4_MayorLakeCabin(void);
extern void sub_StateChange_HouseInteriors4_RanchHouseEast(void);
extern void sub_StateChange_HouseInteriors4_RanchHouseWest(void);
extern void sub_StateChange_HouseInteriors4_Swiftblade(void);
extern void sub_StateChange_HyruleCastle_0(void);
extern void sub_StateChange_HyruleCastle_1(void);
extern void sub_StateChange_HyruleCastle_2(void);
extern void sub_StateChange_HyruleCastle_3(void);
extern void sub_StateChange_HyruleCastle_4(void);
extern void sub_StateChange_HyruleCastle_5(void);
extern void sub_StateChange_HyruleField_EasternHillsCenter(void);
extern void sub_StateChange_HyruleField_EasternHillsNorth(void);
extern void sub_StateChange_HyruleField_EasternHillsSouth(void);
extern void sub_StateChange_HyruleField_LonLonRanch(void);
extern void sub_StateChange_HyruleField_OutsideCastle(void);
extern void sub_StateChange_HyruleField_SouthHyruleField(void);
extern void sub_StateChange_HyruleField_TrilbyHighlands(void);
extern void sub_StateChange_HyruleField_WesternWoodSouth(void);
extern void sub_StateChange_HyruleField_WesternWoodsCenter(void);
extern void sub_StateChange_HyruleField_WesternWoodsNorth(void);
extern void sub_StateChange_HyruleTownMinishCaves_CrossIntersection(void);
extern void sub_StateChange_HyruleTownMinishCaves_Entrance(void);
extern void sub_StateChange_HyruleTownMinishCaves_Entrance2(void);
extern void sub_StateChange_HyruleTownMinishCaves_Flippers(void);
extern void sub_StateChange_HyruleTownMinishCaves_Librari(void);
extern void sub_StateChange_HyruleTownMinishCaves_MulldozerFight(void);
extern void sub_StateChange_HyruleTownMinishCaves_NorthRoom(void);
extern void sub_StateChange_HyruleTownMinishCaves_PacciJump(void);
extern void sub_StateChange_HyruleTownMinishCaves_SoutheastCorner(void);
extern void sub_StateChange_HyruleTownMinishCaves_WestChest(void);
extern void sub_StateChange_HyruleTownMinishCaves_WestFrozenChest(void);
extern void sub_StateChange_HyruleTownUnderground_Main(void);
extern void sub_StateChange_HyruleTownUnderground_Well(void);
extern void sub_StateChange_HyruleTown_0(void);
extern void sub_StateChange_LakeHylia_Main(void);
extern void sub_StateChange_MelarisMine_Main(void);
extern void sub_StateChange_MinishCaves_BeanPesto(void);
extern void sub_StateChange_MinishCaves_LakeHyliaLibrari(void);
extern void sub_StateChange_MinishCaves_LakeHyliaNorth(void);
extern void sub_StateChange_MinishCaves_MinishWoodsNorth1(void);
extern void sub_StateChange_MinishCaves_MinishWoodsNorth2(void);
extern void sub_StateChange_MinishCaves_MinishWoodsSouthwest(void);
extern void sub_StateChange_MinishCaves_OutsideLinksHouse(void);
extern void sub_StateChange_MinishCaves_Ruins(void);
extern void sub_StateChange_MinishCaves_SoutheastWater1(void);
extern void sub_StateChange_MinishCaves_SoutheastWater2(void);
extern void sub_StateChange_MinishCracks_10(void);
extern void sub_StateChange_MinishCracks_11(void);
extern void sub_StateChange_MinishCracks_5(void);
extern void sub_StateChange_MinishCracks_CastorWildsBow(void);
extern void sub_StateChange_MinishCracks_CastorWildsMiddle(void);
extern void sub_StateChange_MinishCracks_CastorWildsNextToBow(void);
extern void sub_StateChange_MinishCracks_CastorWildsNorth(void);
extern void sub_StateChange_MinishCracks_CastorWildsWest(void);
extern void sub_StateChange_MinishCracks_E(void);
extern void sub_StateChange_MinishCracks_EastHyruleCastle(void);
extern void sub_StateChange_MinishCracks_F(void);
extern void sub_StateChange_MinishCracks_HyruleCastleGarden(void);
extern void sub_StateChange_MinishCracks_LakeHyliaEast(void);
extern void sub_StateChange_MinishCracks_LonLonRanchNorth(void);
extern void sub_StateChange_MinishCracks_MinishWoodsSouth(void);
extern void sub_StateChange_MinishCracks_MtCrenel(void);
extern void sub_StateChange_MinishCracks_RuinsEntrance(void);
extern void sub_StateChange_MinishCracks_RuinsTektite(void);
extern void sub_StateChange_MinishHouseInteriors_BarrelMinish(void);
extern void sub_StateChange_MinishHouseInteriors_Blue(void);
extern void sub_StateChange_MinishHouseInteriors_Festari(void);
extern void sub_StateChange_MinishHouseInteriors_GentariExit(void);
extern void sub_StateChange_MinishHouseInteriors_GentariMain(void);
extern void sub_StateChange_MinishHouseInteriors_Green(void);
extern void sub_StateChange_MinishHouseInteriors_HyruleFieldExit(void);
extern void sub_StateChange_MinishHouseInteriors_HyruleFieldSouthwest(void);
extern void sub_StateChange_MinishHouseInteriors_HyruleTown(void);
extern void sub_StateChange_MinishHouseInteriors_LakeHyliaOcarina(void);
extern void sub_StateChange_MinishHouseInteriors_Librari(void);
extern void sub_StateChange_MinishHouseInteriors_MelariMinesEast(void);
extern void sub_StateChange_MinishHouseInteriors_MelariMinesSoutheast(void);
extern void sub_StateChange_MinishHouseInteriors_MelariMinesSouthwest(void);
extern void sub_StateChange_MinishHouseInteriors_MinishWoodsBomb(void);
extern void sub_StateChange_MinishHouseInteriors_NextToKnuckle(void);
extern void sub_StateChange_MinishHouseInteriors_PotMinish(void);
extern void sub_StateChange_MinishHouseInteriors_Red(void);
extern void sub_StateChange_MinishHouseInteriors_ShoeMinish(void);
extern void sub_StateChange_MinishHouseInteriors_SideArea(void);
extern void sub_StateChange_MinishHouseInteriors_SouthHyruleField(void);
extern void sub_StateChange_MinishPaths_CastorWilds(void);
extern void sub_StateChange_MinishPaths_HyruleTown(void);
extern void sub_StateChange_MinishPaths_LonLonRanch(void);
extern void sub_StateChange_MinishPaths_MayorsCabin(void);
extern void sub_StateChange_MinishPaths_ToMinishVillage(void);
extern void sub_StateChange_MinishRafters_Bakery(void);
extern void sub_StateChange_MinishRafters_Cafe(void);
extern void sub_StateChange_MinishRafters_DrLeft(void);
extern void sub_StateChange_MinishRafters_Stockwell(void);
extern void sub_StateChange_MinishVillage_Main(void);
extern void sub_StateChange_MinishVillage_SideHouse(void);
extern void sub_StateChange_MinishWoods_Main(void);
extern void sub_StateChange_MtCrenel_CaveOfFlamesEntrance(void);
extern void sub_StateChange_MtCrenel_Entrance(void);
extern void sub_StateChange_MtCrenel_GustJarShortcut(void);
extern void sub_StateChange_MtCrenel_MountainTop(void);
extern void sub_StateChange_MtCrenel_WallClimb(void);
extern void sub_StateChange_OuterFortressOfWinds_2F(void);
extern void sub_StateChange_OuterFortressOfWinds_3F(void);
extern void sub_StateChange_OuterFortressOfWinds_EntranceHall(void);
extern void sub_StateChange_OuterFortressOfWinds_MoleMitts(void);
extern void sub_StateChange_OuterFortressOfWinds_SmallKey(void);
extern void sub_StateChange_PalaceOfWindsBoss_Main(void);
extern void sub_StateChange_PalaceOfWinds_BallAndChainSoldiers(void);
extern void sub_StateChange_PalaceOfWinds_BeforeBallAndChainSoldiers(void);
extern void sub_StateChange_PalaceOfWinds_BlockMazeToBossDoor(void);
extern void sub_StateChange_PalaceOfWinds_BombWallInside(void);
extern void sub_StateChange_PalaceOfWinds_BombWallOutside(void);
extern void sub_StateChange_PalaceOfWinds_BombarossaPath(void);
extern void sub_StateChange_PalaceOfWinds_BossKey(void);
extern void sub_StateChange_PalaceOfWinds_BridgeAfterDarknut(void);
extern void sub_StateChange_PalaceOfWinds_BridgeSwitchesCloneBlock(void);
extern void sub_StateChange_PalaceOfWinds_CloudJumps(void);
extern void sub_StateChange_PalaceOfWinds_CornerToMap(void);
extern void sub_StateChange_PalaceOfWinds_CrackedFloorLakitu(void);
extern void sub_StateChange_PalaceOfWinds_CrowRide(void);
extern void sub_StateChange_PalaceOfWinds_DarkCompassHall(void);
extern void sub_StateChange_PalaceOfWinds_DarknutMiniboss(void);
extern void sub_StateChange_PalaceOfWinds_DoorToStalfosFirebar(void);
extern void sub_StateChange_PalaceOfWinds_EastChestFromGyorgBossDoor(void);
extern void sub_StateChange_PalaceOfWinds_EntranceRoom(void);
extern void sub_StateChange_PalaceOfWinds_FanAndKeyToBossKey(void);
extern void sub_StateChange_PalaceOfWinds_FanBridge(void);
extern void sub_StateChange_PalaceOfWinds_FireBarGrates(void);
extern void sub_StateChange_PalaceOfWinds_FloormasterLever(void);
extern void sub_StateChange_PalaceOfWinds_FourButtonStalfos(void);
extern void sub_StateChange_PalaceOfWinds_GibdoStairs(void);
extern void sub_StateChange_PalaceOfWinds_GratePlatformRide(void);
extern void sub_StateChange_PalaceOfWinds_GratesTo3F(void);
extern void sub_StateChange_PalaceOfWinds_GyorgBossDoor(void);
extern void sub_StateChange_PalaceOfWinds_GyorgTornado(void);
extern void sub_StateChange_PalaceOfWinds_HeartPieceBridge(void);
extern void sub_StateChange_PalaceOfWinds_HoleToDarknut(void);
extern void sub_StateChange_PalaceOfWinds_HoleToKinstoneWizzrobe(void);
extern void sub_StateChange_PalaceOfWinds_KeyArrowButton(void);
extern void sub_StateChange_PalaceOfWinds_KinstoneWizzrobeFight(void);
extern void sub_StateChange_PalaceOfWinds_Map(void);
extern void sub_StateChange_PalaceOfWinds_MoblinAndWizzrobeFight(void);
extern void sub_StateChange_PalaceOfWinds_PeahatSwitch(void);
extern void sub_StateChange_PalaceOfWinds_PitCornerAfterKey(void);
extern void sub_StateChange_PalaceOfWinds_PlatformCloneRide(void);
extern void sub_StateChange_PalaceOfWinds_PlatformRideBombarossas(void);
extern void sub_StateChange_PalaceOfWinds_PotPush(void);
extern void sub_StateChange_PalaceOfWinds_RedWarpHall(void);
extern void sub_StateChange_PalaceOfWinds_RocCape(void);
extern void sub_StateChange_PalaceOfWinds_ShortcutDoorButtons(void);
extern void sub_StateChange_PalaceOfWinds_SpikeBarSmallKey(void);
extern void sub_StateChange_PalaceOfWinds_SpinyFight(void);
extern void sub_StateChange_PalaceOfWinds_StairsAfterFloormaster(void);
extern void sub_StateChange_PalaceOfWinds_StalfosFireborHole(void);
extern void sub_StateChange_PalaceOfWinds_ToBombarossaPath(void);
extern void sub_StateChange_PalaceOfWinds_ToFanBridge(void);
extern void sub_StateChange_PalaceOfWinds_ToPeahatSwitch(void);
extern void sub_StateChange_PalaceOfWinds_WhirlwindBombarossa(void);
extern void sub_StateChange_RoyalCrypt_Entrance(void);
extern void sub_StateChange_RoyalCrypt_KingGustaf(void);
extern void sub_StateChange_RoyalCrypt_MushroomPit(void);
extern void sub_StateChange_RoyalValleyGraves_Gina(void);
extern void sub_StateChange_RoyalValleyGraves_HeartPiece(void);
extern void sub_StateChange_RoyalValley_ForestMaze(void);
extern void sub_StateChange_RoyalValley_Main(void);
extern void sub_StateChange_Ruins_Armos(void);
extern void sub_StateChange_Ruins_Beanstalk(void);
extern void sub_StateChange_Ruins_Beanstalk0(void);
extern void sub_StateChange_Ruins_Beanstalk1(void);
extern void sub_StateChange_Ruins_Beanstalk2(void);
extern void sub_StateChange_Ruins_Beanstalk3(void);
extern void sub_StateChange_Ruins_Beanstalk4(void);
extern void sub_StateChange_Ruins_Beanstalk5(void);
extern void sub_StateChange_Ruins_Beanstalk6(void);
extern void sub_StateChange_Ruins_Beanstalk7(void);
extern void sub_StateChange_Ruins_Beanstalk8(void);
extern void sub_StateChange_Ruins_Beanstalk9(void);
extern void sub_StateChange_Ruins_Entrance(void);
extern void sub_StateChange_Ruins_FortressEntrance(void);
extern void sub_StateChange_Ruins_LadderToTektites(void);
extern void sub_StateChange_Ruins_LadderToTektites0(void);
extern void sub_StateChange_Ruins_LadderToTektites1(void);
extern void sub_StateChange_Ruins_LadderToTektites2(void);
extern void sub_StateChange_Ruins_LadderToTektites3(void);
extern void sub_StateChange_Ruins_LadderToTektites4(void);
extern void sub_StateChange_Ruins_LadderToTektites5(void);
extern void sub_StateChange_Ruins_LadderToTektites6(void);
extern void sub_StateChange_Ruins_LadderToTektites7(void);
extern void sub_StateChange_Ruins_LadderToTektites8(void);
extern void sub_StateChange_Ruins_LadderToTektites9(void);
extern void sub_StateChange_Ruins_TripleTektites(void);
extern void sub_StateChange_SanctuaryEntrance_Main(void);
extern void sub_StateChange_Sanctuary_Hall(void);
extern void sub_StateChange_Sanctuary_Main(void);
extern void sub_StateChange_Sanctuary_StainedGlass(void);
extern void sub_StateChange_SimonsSimulation_Main(void);
extern void sub_StateChange_TempleOfDroplets_BigBlueChuchu(void);
extern void sub_StateChange_TempleOfDroplets_BigBlueChuchuKey(void);
extern void sub_StateChange_TempleOfDroplets_BigOcto(void);
extern void sub_StateChange_TempleOfDroplets_BlueChuchuKeyLever(void);
extern void sub_StateChange_TempleOfDroplets_EastHole(void);
extern void sub_StateChange_TempleOfDroplets_Element(void);
extern void sub_StateChange_TempleOfDroplets_Entrance(void);
extern void sub_StateChange_TempleOfDroplets_HoleToBlueChuchu(void);
extern void sub_StateChange_TempleOfDroplets_IceCorner(void);
extern void sub_StateChange_TempleOfDroplets_IcePitMaze(void);
extern void sub_StateChange_TempleOfDroplets_LilypadIceBlocks(void);
extern void sub_StateChange_TempleOfDroplets_NorthSplit(void);
extern void sub_StateChange_TempleOfDroplets_NorthwestStairs(void);
extern void sub_StateChange_TempleOfDroplets_ScissorsMiniboss(void);
extern void sub_StateChange_TempleOfDroplets_WaterfallNortheast(void);
extern void sub_StateChange_TempleOfDroplets_WaterfallNorthwest(void);
extern void sub_StateChange_TempleOfDroplets_WaterfallSoutheast(void);
extern void sub_StateChange_TempleOfDroplets_WaterfallSouthwest(void);
extern void sub_StateChange_TempleOfDroplets_WestHole(void);
extern void sub_StateChange_TownMinishHoles_5(void);
extern void sub_StateChange_TownMinishHoles_Cafe(void);
extern void sub_StateChange_TownMinishHoles_Carpenter(void);
extern void sub_StateChange_TownMinishHoles_DrLeft(void);
extern void sub_StateChange_TownMinishHoles_LibrariBookHouse(void);
extern void sub_StateChange_TownMinishHoles_LibraryBookshelf(void);
extern void sub_StateChange_TownMinishHoles_MayorsHouse(void);
extern void sub_StateChange_TownMinishHoles_RemShoeShop(void);
extern void sub_StateChange_TownMinishHoles_WestOracle(void);
extern void sub_StateChange_TreeInteriors_14(void);
extern void sub_StateChange_TreeInteriors_1C(void);
extern void sub_StateChange_TreeInteriors_1E(void);
extern void sub_StateChange_TreeInteriors_BoomerangNortheast(void);
extern void sub_StateChange_TreeInteriors_BoomerangNorthwest(void);
extern void sub_StateChange_TreeInteriors_BoomerangSoutheast(void);
extern void sub_StateChange_TreeInteriors_BoomerangSouthwest(void);
extern void sub_StateChange_TreeInteriors_HeartPiece(void);
extern void sub_StateChange_TreeInteriors_MinishWoodsBusinessScrub(void);
extern void sub_StateChange_TreeInteriors_MinishWoodsGreatFairy(void);
extern void sub_StateChange_TreeInteriors_NorthHyruleFieldFairyFountain(void);
extern void sub_StateChange_TreeInteriors_PercysTreehouse(void);
extern void sub_StateChange_TreeInteriors_StairsToCarlov(void);
extern void sub_StateChange_TreeInteriors_UnusedHeartContainer(void);
extern void sub_StateChange_TreeInteriors_Waveblade(void);
extern void sub_StateChange_TreeInteriors_WesternWoodsHeartPiece(void);
extern void sub_StateChange_TreeInteriors_WitchHut(void);
extern void sub_StateChange_Vaati2_Main(void);
extern void sub_StateChange_Vaati3_Main(void);
extern void sub_StateChange_VaatisArms_First(void);
extern void sub_StateChange_VaatisArms_Second(void);
extern void sub_StateChange_VeilFallsCaves_BlockPuzzle(void);
extern void sub_StateChange_VeilFallsCaves_Entrance(void);
extern void sub_StateChange_VeilFallsCaves_Exit(void);
extern void sub_StateChange_VeilFallsCaves_Hallway1F(void);
extern void sub_StateChange_VeilFallsCaves_Hallway2F(void);
extern void sub_StateChange_VeilFallsCaves_HeartPiece(void);
extern void sub_StateChange_VeilFallsCaves_RupeePath(void);
extern void sub_StateChange_VeilFallsCaves_SecretChest(void);
extern void sub_StateChange_VeilFallsCaves_SecretRoom(void);
extern void sub_StateChange_VeilFallsCaves_SecretStaircases(void);
extern void sub_StateChange_VeilFallsDigCave_Main(void);
extern void sub_StateChange_VeilFallsTop_Main(void);
extern void sub_StateChange_VeilFalls_Main(void);
extern void sub_StateChange_WindTribeTowerRoof_Main(void);
extern void sub_StateChange_WindTribeTower_Entrance(void);
extern void sub_StateChange_WindTribeTower_Floor2(void);
extern void sub_StateChange_WindTribeTower_Floor3(void);
extern void sub_StateChange_WindTribeTower_Floor4(void);
extern u32 sub_unk1_CastorWilds_Main(void);
extern u32 sub_unk1_CloudTops_House(void);
extern u32 sub_unk1_CrenelMinishPaths_CrenelWater(void);
extern u32 sub_unk1_CrenelMinishPaths_MelarisMine(void);
extern u32 sub_unk1_CrenelMinishPaths_Rainfall(void);
extern u32 sub_unk1_HyruleField_SouthHyruleField(void);
extern u32 sub_unk1_HyruleTown_8(void);
extern u32 sub_unk1_MinishPaths_CastorWilds(void);
extern u32 sub_unk1_MinishPaths_HyruleTown(void);
extern u32 sub_unk1_MinishPaths_LonLonRanch(void);
extern u32 sub_unk1_MinishPaths_MayorsCabin(void);
extern u32 sub_unk1_MinishPaths_ToMinishVillage(void);
extern u32 sub_unk1_MinishRafters_DrLeft(void);
extern u32 sub_unk1_MinishWoods_Main(void);
extern u32 sub_unk1_MtCrenel_CaveOfFlamesEntrance(void);
extern u32 sub_unk1_MtCrenel_MountainTop(void);
extern u32 sub_unk1_Ruins_TripleTektites(void);
extern u32 sub_unk1_VeilFalls_Main(void);
extern void sub_unk2_MinishVillage_SideHouse(void);
extern u32 sub_unk3_37_0(void);
extern u32 sub_unk3_37_1(void);
extern u32 sub_unk3_45_Main(void);
extern u32 sub_unk3_47_0(void);
extern u32 sub_unk3_47_1(void);
extern u32 sub_unk3_47_2(void);
extern u32 sub_unk3_47_3(void);
extern u32 sub_unk3_47_4(void);
extern u32 sub_unk3_4D_Main(void);
extern u32 sub_unk3_57_Main(void);
extern u32 sub_unk3_5F_Main(void);
extern u32 sub_unk3_67_Main(void);
extern u32 sub_unk3_6F_Main(void);
extern u32 sub_unk3_77_Main(void);
extern u32 sub_unk3_7F_Main(void);
extern u32 sub_unk3_87_Main(void);
extern u32 sub_unk3_8F_Main(void);
extern u32 sub_unk3_ArmosInteriors_6(void);
extern u32 sub_unk3_ArmosInteriors_8(void);
extern u32 sub_unk3_ArmosInteriors_FortressOfWindsLeft(void);
extern u32 sub_unk3_ArmosInteriors_FortressOfWindsRight(void);
extern u32 sub_unk3_ArmosInteriors_RuinsEntranceNorth(void);
extern u32 sub_unk3_ArmosInteriors_RuinsEntranceSouth(void);
extern u32 sub_unk3_ArmosInteriors_RuinsGrassPath(void);
extern u32 sub_unk3_ArmosInteriors_RuinsLeft(void);
extern u32 sub_unk3_ArmosInteriors_RuinsMiddleLeft(void);
extern u32 sub_unk3_ArmosInteriors_RuinsMiddleRight(void);
extern u32 sub_unk3_ArmosInteriors_RuinsRight(void);
extern u32 sub_unk3_Beanstalks_EasternHills(void);
extern u32 sub_unk3_Beanstalks_EasternHillsClimb(void);
extern u32 sub_unk3_Beanstalks_LakeHylia(void);
extern u32 sub_unk3_Beanstalks_LakeHyliaClimb(void);
extern u32 sub_unk3_Beanstalks_MountCrenel(void);
extern u32 sub_unk3_Beanstalks_MountCrenelClimb(void);
extern u32 sub_unk3_Beanstalks_Ruins(void);
extern u32 sub_unk3_Beanstalks_RuinsClimb(void);
extern u32 sub_unk3_Beanstalks_WesternWoods(void);
extern u32 sub_unk3_Beanstalks_WesternWoodsClimb(void);
extern u32 sub_unk3_CastleGardenMinishHoles_East(void);
extern u32 sub_unk3_CastleGardenMinishHoles_West(void);
extern u32 sub_unk3_CastleGarden_Main(void);
extern u32 sub_unk3_CastorCaves_Darknut(void);
extern u32 sub_unk3_CastorCaves_HeartPiece(void);
extern u32 sub_unk3_CastorCaves_North(void);
extern u32 sub_unk3_CastorCaves_South(void);
extern u32 sub_unk3_CastorCaves_WindRuins(void);
extern u32 sub_unk3_CastorDarknut_Hall(void);
extern u32 sub_unk3_CastorDarknut_Main(void);
extern u32 sub_unk3_CastorWildsDigCave_Main(void);
extern u32 sub_unk3_CastorWilds_Main(void);
extern u32 sub_unk3_CaveOfFlamesBoss_Main(void);
extern u32 sub_unk3_CaveOfFlames_18(void);
extern u32 sub_unk3_CaveOfFlames_AfterCane(void);
extern u32 sub_unk3_CaveOfFlames_BeforeGleerok(void);
extern u32 sub_unk3_CaveOfFlames_Bobomb(void);
extern u32 sub_unk3_CaveOfFlames_BossDoor(void);
extern u32 sub_unk3_CaveOfFlames_CartToSpinyChus(void);
extern u32 sub_unk3_CaveOfFlames_CartWest(void);
extern u32 sub_unk3_CaveOfFlames_Compass(void);
extern u32 sub_unk3_CaveOfFlames_Entrance(void);
extern u32 sub_unk3_CaveOfFlames_Helmasaur(void);
extern u32 sub_unk3_CaveOfFlames_MainCart(void);
extern u32 sub_unk3_CaveOfFlames_MinishLava(void);
extern u32 sub_unk3_CaveOfFlames_MinishSpikes(void);
extern u32 sub_unk3_CaveOfFlames_NorthEntrance(void);
extern u32 sub_unk3_CaveOfFlames_PathBossKey(void);
extern u32 sub_unk3_CaveOfFlames_PathBossKey2(void);
extern u32 sub_unk3_CaveOfFlames_Rollobite(void);
extern u32 sub_unk3_CaveOfFlames_RollobiteSwitch(void);
extern u32 sub_unk3_CaveOfFlames_SpinyChus(void);
extern u32 sub_unk3_Caves_2(void);
extern u32 sub_unk3_Caves_3(void);
extern u32 sub_unk3_Caves_4(void);
extern u32 sub_unk3_Caves_5(void);
extern u32 sub_unk3_Caves_6(void);
extern u32 sub_unk3_Caves_A(void);
extern u32 sub_unk3_Caves_Boomerang(void);
extern u32 sub_unk3_Caves_BottleBusinessScrub(void);
extern u32 sub_unk3_Caves_HeartPieceHallway(void);
extern u32 sub_unk3_Caves_HillsKeeseChest(void);
extern u32 sub_unk3_Caves_HyruleTownWaterfall(void);
extern u32 sub_unk3_Caves_KinstoneBusinessScrub(void);
extern u32 sub_unk3_Caves_LonLonRanch(void);
extern u32 sub_unk3_Caves_LonLonRanchSecret(void);
extern u32 sub_unk3_Caves_LonLonRanchWallet(void);
extern u32 sub_unk3_Caves_NorthHyruleFieldFairyFountain(void);
extern u32 sub_unk3_Caves_SouthHyruleFieldFairyFountain(void);
extern u32 sub_unk3_Caves_SouthHyruleFieldRupee(void);
extern u32 sub_unk3_Caves_ToGraveyard(void);
extern u32 sub_unk3_Caves_TrilbyFairyFountain(void);
extern u32 sub_unk3_Caves_TrilbyHighlands(void);
extern u32 sub_unk3_Caves_TrilbyKeeseChest(void);
extern u32 sub_unk3_Caves_TrilbyMittsFairyFountain(void);
extern u32 sub_unk3_Caves_TrilbyRupee(void);
extern u32 sub_unk3_CloudTops_Bottom(void);
extern u32 sub_unk3_CloudTops_House(void);
extern u32 sub_unk3_CloudTops_Middle(void);
extern u32 sub_unk3_CrenelCaves_BlockPushing(void);
extern u32 sub_unk3_CrenelCaves_BombBusinessScrub(void);
extern u32 sub_unk3_CrenelCaves_BridgeSwitch(void);
extern u32 sub_unk3_CrenelCaves_ChuchuPotChest(void);
extern u32 sub_unk3_CrenelCaves_ExitToMines(void);
extern u32 sub_unk3_CrenelCaves_FairyFountain(void);
extern u32 sub_unk3_CrenelCaves_GripRing(void);
extern u32 sub_unk3_CrenelCaves_HelmasaurHallway(void);
extern u32 sub_unk3_CrenelCaves_Hermit(void);
extern u32 sub_unk3_CrenelCaves_HintScrub(void);
extern u32 sub_unk3_CrenelCaves_LadderToSpringWater(void);
extern u32 sub_unk3_CrenelCaves_MushroomKeese(void);
extern u32 sub_unk3_CrenelCaves_PillarCave(void);
extern u32 sub_unk3_CrenelCaves_RupeeFairyFountain(void);
extern u32 sub_unk3_CrenelCaves_SpinyChuPuzzle(void);
extern u32 sub_unk3_CrenelCaves_ToGrayblade(void);
extern u32 sub_unk3_CrenelCaves_WaterHeartPiece(void);
extern u32 sub_unk3_CrenelDigCave_Main(void);
extern u32 sub_unk3_CrenelMinishPaths_CrenelBean(void);
extern u32 sub_unk3_CrenelMinishPaths_CrenelWater(void);
extern u32 sub_unk3_CrenelMinishPaths_MelarisMine(void);
extern u32 sub_unk3_CrenelMinishPaths_Rainfall(void);
extern u32 sub_unk3_DarkHyruleCastleBridge_Main(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_8(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_Garden(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_OutsideEast(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_OutsideNortheast(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_OutsideNorthwest(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_OutsideSouth(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_OutsideSoutheast(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_OutsideSouthwest(void);
extern u32 sub_unk3_DarkHyruleCastleOutside_ZeldaStatuePlatform(void);
extern u32 sub_unk3_DarkHyruleCastle_1FBeforeThrone(void);
extern u32 sub_unk3_DarkHyruleCastle_1FBottomLeftTower(void);
extern u32 sub_unk3_DarkHyruleCastle_1FBottomRightTower(void);
extern u32 sub_unk3_DarkHyruleCastle_1FCompass(void);
extern u32 sub_unk3_DarkHyruleCastle_1FEntrance(void);
extern u32 sub_unk3_DarkHyruleCastle_1FLoopBottom(void);
extern u32 sub_unk3_DarkHyruleCastle_1FLoopBottomLeft(void);
extern u32 sub_unk3_DarkHyruleCastle_1FLoopBottomRight(void);
extern u32 sub_unk3_DarkHyruleCastle_1FLoopLeft(void);
extern u32 sub_unk3_DarkHyruleCastle_1FLoopRight(void);
extern u32 sub_unk3_DarkHyruleCastle_1FLoopTop(void);
extern u32 sub_unk3_DarkHyruleCastle_1FLoopTopLeft(void);
extern u32 sub_unk3_DarkHyruleCastle_1FLoopTopRight(void);
extern u32 sub_unk3_DarkHyruleCastle_1FThroneRoom(void);
extern u32 sub_unk3_DarkHyruleCastle_1FTopLeftTower(void);
extern u32 sub_unk3_DarkHyruleCastle_1FTopRightTower(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBlueWarp(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBossDoor(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBossKey(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBottomLeftCorner(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBottomLeftCornerPuzzle(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBottomLeftDarknuts(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBottomLeftGhini(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBottomLeftTower(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBottomRightDarknut(void);
extern u32 sub_unk3_DarkHyruleCastle_2FBottomRightTower(void);
extern u32 sub_unk3_DarkHyruleCastle_2FEntrance(void);
extern u32 sub_unk3_DarkHyruleCastle_2FLeft(void);
extern u32 sub_unk3_DarkHyruleCastle_2FRight(void);
extern u32 sub_unk3_DarkHyruleCastle_2FSparks(void);
extern u32 sub_unk3_DarkHyruleCastle_2FTopLeftCorner(void);
extern u32 sub_unk3_DarkHyruleCastle_2FTopLeftDarknut(void);
extern u32 sub_unk3_DarkHyruleCastle_2FTopLeftTower(void);
extern u32 sub_unk3_DarkHyruleCastle_2FTopRightCornerGhini(void);
extern u32 sub_unk3_DarkHyruleCastle_2FTopRightCornerTorches(void);
extern u32 sub_unk3_DarkHyruleCastle_2FTopRightDarknuts(void);
extern u32 sub_unk3_DarkHyruleCastle_2FTopRightTower(void);
extern u32 sub_unk3_DarkHyruleCastle_3FBottomLeftTower(void);
extern u32 sub_unk3_DarkHyruleCastle_3FBottomRightTower(void);
extern u32 sub_unk3_DarkHyruleCastle_3FKeatonHallToVaati(void);
extern u32 sub_unk3_DarkHyruleCastle_3FTopLeftTower(void);
extern u32 sub_unk3_DarkHyruleCastle_3FTopRightTower(void);
extern u32 sub_unk3_DarkHyruleCastle_3FTopRightTower7(void);
extern u32 sub_unk3_DarkHyruleCastle_3FTripleDarknut(void);
extern u32 sub_unk3_DarkHyruleCastle_B1BeforeThrone(void);
extern u32 sub_unk3_DarkHyruleCastle_B1BelowCompass(void);
extern u32 sub_unk3_DarkHyruleCastle_B1BelowThrone(void);
extern u32 sub_unk3_DarkHyruleCastle_B1BombWall(void);
extern u32 sub_unk3_DarkHyruleCastle_B1Cannons(void);
extern u32 sub_unk3_DarkHyruleCastle_B1Entrance(void);
extern u32 sub_unk3_DarkHyruleCastle_B1Keatons(void);
extern u32 sub_unk3_DarkHyruleCastle_B1Left(void);
extern u32 sub_unk3_DarkHyruleCastle_B1Map(void);
extern u32 sub_unk3_DarkHyruleCastle_B1Right(void);
extern u32 sub_unk3_DarkHyruleCastle_B1ToPrison(void);
extern u32 sub_unk3_DarkHyruleCastle_B1ToPrisonFirebar(void);
extern u32 sub_unk3_DarkHyruleCastle_B2Dropdown(void);
extern u32 sub_unk3_DarkHyruleCastle_B2Prison(void);
extern u32 sub_unk3_DarkHyruleCastle_B2ToPrison(void);
extern u32 sub_unk3_DeepwoodShrineBoss_Main(void);
extern u32 sub_unk3_DeepwoodShrineEntry_Main(void);
extern u32 sub_unk3_DeepwoodShrine_13(void);
extern u32 sub_unk3_DeepwoodShrine_Barrel(void);
extern u32 sub_unk3_DeepwoodShrine_BluePortal(void);
extern u32 sub_unk3_DeepwoodShrine_BossKey(void);
extern u32 sub_unk3_DeepwoodShrine_Button(void);
extern u32 sub_unk3_DeepwoodShrine_Compass(void);
extern u32 sub_unk3_DeepwoodShrine_DoubleStatue(void);
extern u32 sub_unk3_DeepwoodShrine_Entrance(void);
extern u32 sub_unk3_DeepwoodShrine_InsideBarrel(void);
extern u32 sub_unk3_DeepwoodShrine_Lever(void);
extern u32 sub_unk3_DeepwoodShrine_LilyPadEast(void);
extern u32 sub_unk3_DeepwoodShrine_LilyPadWest(void);
extern u32 sub_unk3_DeepwoodShrine_Madderpillar(void);
extern u32 sub_unk3_DeepwoodShrine_Map(void);
extern u32 sub_unk3_DeepwoodShrine_Mulldozer(void);
extern u32 sub_unk3_DeepwoodShrine_Pillars(void);
extern u32 sub_unk3_DeepwoodShrine_PotBridge(void);
extern u32 sub_unk3_DeepwoodShrine_PreBoss(void);
extern u32 sub_unk3_DeepwoodShrine_StairsToB1(void);
extern u32 sub_unk3_DeepwoodShrine_Torch(void);
extern u32 sub_unk3_DigCaves1_HyruleFieldFarm(void);
extern u32 sub_unk3_DigCaves1_TrilbyHighlands(void);
extern u32 sub_unk3_Dojos_7(void);
extern u32 sub_unk3_Dojos_8(void);
extern u32 sub_unk3_Dojos_9(void);
extern u32 sub_unk3_Dojos_Grayblade(void);
extern u32 sub_unk3_Dojos_Greatblade(void);
extern u32 sub_unk3_Dojos_Grimblade(void);
extern u32 sub_unk3_Dojos_Scarblade(void);
extern u32 sub_unk3_Dojos_Splitblade(void);
extern u32 sub_unk3_Dojos_Swiftblade(void);
extern u32 sub_unk3_Dojos_ToGreatblade(void);
extern u32 sub_unk3_Dojos_ToGrimblade(void);
extern u32 sub_unk3_Dojos_ToScarblade(void);
extern u32 sub_unk3_Dojos_ToSplitblade(void);
extern u32 sub_unk3_Dojos_Waveblade(void);
extern u32 sub_unk3_Empty_Main(void);
extern u32 sub_unk3_EzloAuxCutscene_Main(void);
extern u32 sub_unk3_FortressOfWindsTop_Main(void);
extern u32 sub_unk3_FortressOfWinds_BeforeMazaal(void);
extern u32 sub_unk3_FortressOfWinds_BossKey(void);
extern u32 sub_unk3_FortressOfWinds_CenterStairs1F(void);
extern u32 sub_unk3_FortressOfWinds_Clone(void);
extern u32 sub_unk3_FortressOfWinds_Darknut(void);
extern u32 sub_unk3_FortressOfWinds_EastKeyLever(void);
extern u32 sub_unk3_FortressOfWinds_EastStairs1F(void);
extern u32 sub_unk3_FortressOfWinds_EastStairs2F(void);
extern u32 sub_unk3_FortressOfWinds_EntranceMoleMitts(void);
extern u32 sub_unk3_FortressOfWinds_EyeBridge(void);
extern u32 sub_unk3_FortressOfWinds_Eyegore(void);
extern u32 sub_unk3_FortressOfWinds_HeartPiece(void);
extern u32 sub_unk3_FortressOfWinds_Main2F(void);
extern u32 sub_unk3_FortressOfWinds_Mazaal(void);
extern u32 sub_unk3_FortressOfWinds_MinishHole(void);
extern u32 sub_unk3_FortressOfWinds_Pit(void);
extern u32 sub_unk3_FortressOfWinds_PitPlatforms(void);
extern u32 sub_unk3_FortressOfWinds_SpikeTraps(void);
extern u32 sub_unk3_FortressOfWinds_Stalfos(void);
extern u32 sub_unk3_FortressOfWinds_Wallmaster(void);
extern u32 sub_unk3_FortressOfWinds_WestKeyLever(void);
extern u32 sub_unk3_FortressOfWinds_WestStairs1F(void);
extern u32 sub_unk3_FortressOfWinds_WestStairs2F(void);
extern u32 sub_unk3_FortressOfWinds_Wizzrobe(void);
extern u32 sub_unk3_GardenFountains_East(void);
extern u32 sub_unk3_GardenFountains_West(void);
extern u32 sub_unk3_GoronCave_Main(void);
extern u32 sub_unk3_GoronCave_Stairs(void);
extern u32 sub_unk3_GreatFairies_Entrance(void);
extern u32 sub_unk3_GreatFairies_Exit(void);
extern u32 sub_unk3_GreatFairies_Graveyard(void);
extern u32 sub_unk3_GreatFairies_MinishWoods(void);
extern u32 sub_unk3_GreatFairies_MtCrenel(void);
extern u32 sub_unk3_HouseInteriors1_Inn1F(void);
extern u32 sub_unk3_HouseInteriors1_InnEast2F(void);
extern u32 sub_unk3_HouseInteriors1_InnEastRoom(void);
extern u32 sub_unk3_HouseInteriors1_InnMiddleRoom(void);
extern u32 sub_unk3_HouseInteriors1_InnMinishHeartPiece(void);
extern u32 sub_unk3_HouseInteriors1_InnWest2F(void);
extern u32 sub_unk3_HouseInteriors1_InnWestRoom(void);
extern u32 sub_unk3_HouseInteriors1_Library1F(void);
extern u32 sub_unk3_HouseInteriors1_Library2F(void);
extern u32 sub_unk3_HouseInteriors1_Mayor(void);
extern u32 sub_unk3_HouseInteriors1_PostOffice(void);
extern u32 sub_unk3_HouseInteriors1_SchoolEast(void);
extern u32 sub_unk3_HouseInteriors1_SchoolWest(void);
extern u32 sub_unk3_HouseInteriors2_2(void);
extern u32 sub_unk3_HouseInteriors2_3(void);
extern u32 sub_unk3_HouseInteriors2_A(void);
extern u32 sub_unk3_HouseInteriors2_B(void);
extern u32 sub_unk3_HouseInteriors2_Cucco(void);
extern u32 sub_unk3_HouseInteriors2_Dampe(void);
extern u32 sub_unk3_HouseInteriors2_DrLeft(void);
extern u32 sub_unk3_HouseInteriors2_EastOracle(void);
extern u32 sub_unk3_HouseInteriors2_Julietta(void);
extern u32 sub_unk3_HouseInteriors2_LinksHouseBedroom(void);
extern u32 sub_unk3_HouseInteriors2_LinksHouseEntrance(void);
extern u32 sub_unk3_HouseInteriors2_LinksHouseSmith(void);
extern u32 sub_unk3_HouseInteriors2_Percy(void);
extern u32 sub_unk3_HouseInteriors2_Romio(void);
extern u32 sub_unk3_HouseInteriors2_StockwellLakeHouse(void);
extern u32 sub_unk3_HouseInteriors2_Stranger(void);
extern u32 sub_unk3_HouseInteriors2_WestOracle(void);
extern u32 sub_unk3_HouseInteriors3_Bakery(void);
extern u32 sub_unk3_HouseInteriors3_Borlov(void);
extern u32 sub_unk3_HouseInteriors3_BorlovEntrance(void);
extern u32 sub_unk3_HouseInteriors3_Cafe(void);
extern u32 sub_unk3_HouseInteriors3_Carlov(void);
extern u32 sub_unk3_HouseInteriors3_FigurineHouse(void);
extern u32 sub_unk3_HouseInteriors3_RemShoeShop(void);
extern u32 sub_unk3_HouseInteriors3_Simon(void);
extern u32 sub_unk3_HouseInteriors3_StockwellShop(void);
extern u32 sub_unk3_HouseInteriors4_Carpenter(void);
extern u32 sub_unk3_HouseInteriors4_FarmHouse(void);
extern u32 sub_unk3_HouseInteriors4_MayorLakeCabin(void);
extern u32 sub_unk3_HouseInteriors4_RanchHouseEast(void);
extern u32 sub_unk3_HouseInteriors4_RanchHouseWest(void);
extern u32 sub_unk3_HouseInteriors4_Swiftblade(void);
extern u32 sub_unk3_HyliaDigCaves_Middle(void);
extern u32 sub_unk3_HyliaDigCaves_North(void);
extern u32 sub_unk3_HyruleCastle_0(void);
extern u32 sub_unk3_HyruleCastle_1(void);
extern u32 sub_unk3_HyruleCastle_2(void);
extern u32 sub_unk3_HyruleCastle_3(void);
extern u32 sub_unk3_HyruleCastle_4(void);
extern u32 sub_unk3_HyruleCastle_5(void);
extern u32 sub_unk3_HyruleDigCaves_Main(void);
extern u32 sub_unk3_HyruleField_EasternHillsCenter(void);
extern u32 sub_unk3_HyruleField_EasternHillsNorth(void);
extern u32 sub_unk3_HyruleField_EasternHillsSouth(void);
extern u32 sub_unk3_HyruleField_LonLonRanch(void);
extern u32 sub_unk3_HyruleField_OutsideCastle(void);
extern u32 sub_unk3_HyruleField_SouthHyruleField(void);
extern u32 sub_unk3_HyruleField_TrilbyHighlands(void);
extern u32 sub_unk3_HyruleField_WesternWoodSouth(void);
extern u32 sub_unk3_HyruleField_WesternWoodsCenter(void);
extern u32 sub_unk3_HyruleField_WesternWoodsNorth(void);
extern u32 sub_unk3_HyruleTownMinishCaves_CrossIntersection(void);
extern u32 sub_unk3_HyruleTownMinishCaves_Entrance(void);
extern u32 sub_unk3_HyruleTownMinishCaves_Entrance2(void);
extern u32 sub_unk3_HyruleTownMinishCaves_Flippers(void);
extern u32 sub_unk3_HyruleTownMinishCaves_Librari(void);
extern u32 sub_unk3_HyruleTownMinishCaves_MulldozerFight(void);
extern u32 sub_unk3_HyruleTownMinishCaves_NorthRoom(void);
extern u32 sub_unk3_HyruleTownMinishCaves_PacciJump(void);
extern u32 sub_unk3_HyruleTownMinishCaves_SoutheastCorner(void);
extern u32 sub_unk3_HyruleTownMinishCaves_WestChest(void);
extern u32 sub_unk3_HyruleTownMinishCaves_WestFrozenChest(void);
extern u32 sub_unk3_HyruleTownUnderground_Main(void);
extern u32 sub_unk3_HyruleTownUnderground_Well(void);
extern u32 sub_unk3_HyruleTown_0(void);
extern u32 sub_unk3_InnerMazaal_Main(void);
extern u32 sub_unk3_LakeHylia_Beanstalk(void);
extern u32 sub_unk3_LakeHylia_Main(void);
extern u32 sub_unk3_LakeWoodsCave_Main(void);
extern u32 sub_unk3_MelarisMine_Main(void);
extern u32 sub_unk3_MinishCaves_BeanPesto(void);
extern u32 sub_unk3_MinishCaves_LakeHyliaLibrari(void);
extern u32 sub_unk3_MinishCaves_LakeHyliaNorth(void);
extern u32 sub_unk3_MinishCaves_MinishWoodsNorth1(void);
extern u32 sub_unk3_MinishCaves_MinishWoodsNorth2(void);
extern u32 sub_unk3_MinishCaves_MinishWoodsSouthwest(void);
extern u32 sub_unk3_MinishCaves_OutsideLinksHouse(void);
extern u32 sub_unk3_MinishCaves_Ruins(void);
extern u32 sub_unk3_MinishCaves_SoutheastWater1(void);
extern u32 sub_unk3_MinishCaves_SoutheastWater2(void);
extern u32 sub_unk3_MinishCracks_10(void);
extern u32 sub_unk3_MinishCracks_11(void);
extern u32 sub_unk3_MinishCracks_5(void);
extern u32 sub_unk3_MinishCracks_CastorWildsBow(void);
extern u32 sub_unk3_MinishCracks_CastorWildsMiddle(void);
extern u32 sub_unk3_MinishCracks_CastorWildsNextToBow(void);
extern u32 sub_unk3_MinishCracks_CastorWildsNorth(void);
extern u32 sub_unk3_MinishCracks_CastorWildsWest(void);
extern u32 sub_unk3_MinishCracks_E(void);
extern u32 sub_unk3_MinishCracks_EastHyruleCastle(void);
extern u32 sub_unk3_MinishCracks_F(void);
extern u32 sub_unk3_MinishCracks_HyruleCastleGarden(void);
extern u32 sub_unk3_MinishCracks_LakeHyliaEast(void);
extern u32 sub_unk3_MinishCracks_LonLonRanchNorth(void);
extern u32 sub_unk3_MinishCracks_MinishWoodsSouth(void);
extern u32 sub_unk3_MinishCracks_MtCrenel(void);
extern u32 sub_unk3_MinishCracks_RuinsEntrance(void);
extern u32 sub_unk3_MinishCracks_RuinsTektite(void);
extern u32 sub_unk3_MinishHouseInteriors_BarrelMinish(void);
extern u32 sub_unk3_MinishHouseInteriors_Blue(void);
extern u32 sub_unk3_MinishHouseInteriors_Festari(void);
extern u32 sub_unk3_MinishHouseInteriors_GentariExit(void);
extern u32 sub_unk3_MinishHouseInteriors_GentariMain(void);
extern u32 sub_unk3_MinishHouseInteriors_Green(void);
extern u32 sub_unk3_MinishHouseInteriors_HyruleFieldExit(void);
extern u32 sub_unk3_MinishHouseInteriors_HyruleFieldSouthwest(void);
extern u32 sub_unk3_MinishHouseInteriors_HyruleTown(void);
extern u32 sub_unk3_MinishHouseInteriors_LakeHyliaOcarina(void);
extern u32 sub_unk3_MinishHouseInteriors_Librari(void);
extern u32 sub_unk3_MinishHouseInteriors_MelariMinesEast(void);
extern u32 sub_unk3_MinishHouseInteriors_MelariMinesSoutheast(void);
extern u32 sub_unk3_MinishHouseInteriors_MelariMinesSouthwest(void);
extern u32 sub_unk3_MinishHouseInteriors_MinishWoodsBomb(void);
extern u32 sub_unk3_MinishHouseInteriors_NextToKnuckle(void);
extern u32 sub_unk3_MinishHouseInteriors_PotMinish(void);
extern u32 sub_unk3_MinishHouseInteriors_Red(void);
extern u32 sub_unk3_MinishHouseInteriors_ShoeMinish(void);
extern u32 sub_unk3_MinishHouseInteriors_SideArea(void);
extern u32 sub_unk3_MinishHouseInteriors_SouthHyruleField(void);
extern u32 sub_unk3_MinishPaths_CastorWilds(void);
extern u32 sub_unk3_MinishPaths_HyruleTown(void);
extern u32 sub_unk3_MinishPaths_LonLonRanch(void);
extern u32 sub_unk3_MinishPaths_MayorsCabin(void);
extern u32 sub_unk3_MinishPaths_ToMinishVillage(void);
extern u32 sub_unk3_MinishRafters_Bakery(void);
extern u32 sub_unk3_MinishRafters_Cafe(void);
extern u32 sub_unk3_MinishRafters_DrLeft(void);
extern u32 sub_unk3_MinishRafters_Stockwell(void);
extern u32 sub_unk3_MinishVillage_Main(void);
extern u32 sub_unk3_MinishVillage_SideHouse(void);
extern u32 sub_unk3_MinishWoods_Main(void);
extern u32 sub_unk3_MtCrenel_CaveOfFlamesEntrance(void);
extern u32 sub_unk3_MtCrenel_Entrance(void);
extern u32 sub_unk3_MtCrenel_GustJarShortcut(void);
extern u32 sub_unk3_MtCrenel_MountainTop(void);
extern u32 sub_unk3_MtCrenel_WallClimb(void);
extern u32 sub_unk3_OuterFortressOfWinds_2F(void);
extern u32 sub_unk3_OuterFortressOfWinds_3F(void);
extern u32 sub_unk3_OuterFortressOfWinds_EntranceHall(void);
extern u32 sub_unk3_OuterFortressOfWinds_MoleMitts(void);
extern u32 sub_unk3_OuterFortressOfWinds_SmallKey(void);
extern u32 sub_unk3_PalaceOfWindsBoss_Main(void);
extern u32 sub_unk3_PalaceOfWinds_BallAndChainSoldiers(void);
extern u32 sub_unk3_PalaceOfWinds_BeforeBallAndChainSoldiers(void);
extern u32 sub_unk3_PalaceOfWinds_BlockMazeToBossDoor(void);
extern u32 sub_unk3_PalaceOfWinds_BombWallInside(void);
extern u32 sub_unk3_PalaceOfWinds_BombWallOutside(void);
extern u32 sub_unk3_PalaceOfWinds_BombarossaPath(void);
extern u32 sub_unk3_PalaceOfWinds_BossKey(void);
extern u32 sub_unk3_PalaceOfWinds_BridgeAfterDarknut(void);
extern u32 sub_unk3_PalaceOfWinds_BridgeSwitchesCloneBlock(void);
extern u32 sub_unk3_PalaceOfWinds_CloudJumps(void);
extern u32 sub_unk3_PalaceOfWinds_CornerToMap(void);
extern u32 sub_unk3_PalaceOfWinds_CrackedFloorLakitu(void);
extern u32 sub_unk3_PalaceOfWinds_CrowRide(void);
extern u32 sub_unk3_PalaceOfWinds_DarkCompassHall(void);
extern u32 sub_unk3_PalaceOfWinds_DarknutMiniboss(void);
extern u32 sub_unk3_PalaceOfWinds_DoorToStalfosFirebar(void);
extern u32 sub_unk3_PalaceOfWinds_EastChestFromGyorgBossDoor(void);
extern u32 sub_unk3_PalaceOfWinds_EntranceRoom(void);
extern u32 sub_unk3_PalaceOfWinds_FanAndKeyToBossKey(void);
extern u32 sub_unk3_PalaceOfWinds_FanBridge(void);
extern u32 sub_unk3_PalaceOfWinds_FireBarGrates(void);
extern u32 sub_unk3_PalaceOfWinds_FloormasterLever(void);
extern u32 sub_unk3_PalaceOfWinds_FourButtonStalfos(void);
extern u32 sub_unk3_PalaceOfWinds_GibdoStairs(void);
extern u32 sub_unk3_PalaceOfWinds_GratePlatformRide(void);
extern u32 sub_unk3_PalaceOfWinds_GratesTo3F(void);
extern u32 sub_unk3_PalaceOfWinds_GyorgBossDoor(void);
extern u32 sub_unk3_PalaceOfWinds_GyorgTornado(void);
extern u32 sub_unk3_PalaceOfWinds_HeartPieceBridge(void);
extern u32 sub_unk3_PalaceOfWinds_HoleToDarknut(void);
extern u32 sub_unk3_PalaceOfWinds_HoleToKinstoneWizzrobe(void);
extern u32 sub_unk3_PalaceOfWinds_KeyArrowButton(void);
extern u32 sub_unk3_PalaceOfWinds_KinstoneWizzrobeFight(void);
extern u32 sub_unk3_PalaceOfWinds_Map(void);
extern u32 sub_unk3_PalaceOfWinds_MoblinAndWizzrobeFight(void);
extern u32 sub_unk3_PalaceOfWinds_PeahatSwitch(void);
extern u32 sub_unk3_PalaceOfWinds_PitCornerAfterKey(void);
extern u32 sub_unk3_PalaceOfWinds_PlatformCloneRide(void);
extern u32 sub_unk3_PalaceOfWinds_PlatformRideBombarossas(void);
extern u32 sub_unk3_PalaceOfWinds_PotPush(void);
extern u32 sub_unk3_PalaceOfWinds_RedWarpHall(void);
extern u32 sub_unk3_PalaceOfWinds_RocCape(void);
extern u32 sub_unk3_PalaceOfWinds_ShortcutDoorButtons(void);
extern u32 sub_unk3_PalaceOfWinds_SpikeBarSmallKey(void);
extern u32 sub_unk3_PalaceOfWinds_SpinyFight(void);
extern u32 sub_unk3_PalaceOfWinds_StairsAfterFloormaster(void);
extern u32 sub_unk3_PalaceOfWinds_StalfosFireborHole(void);
extern u32 sub_unk3_PalaceOfWinds_ToBombarossaPath(void);
extern u32 sub_unk3_PalaceOfWinds_ToFanBridge(void);
extern u32 sub_unk3_PalaceOfWinds_ToPeahatSwitch(void);
extern u32 sub_unk3_PalaceOfWinds_WhirlwindBombarossa(void);
extern u32 sub_unk3_RoyalCrypt_3(void);
extern u32 sub_unk3_RoyalCrypt_5(void);
extern u32 sub_unk3_RoyalCrypt_6(void);
extern u32 sub_unk3_RoyalCrypt_Entrance(void);
extern u32 sub_unk3_RoyalCrypt_Gibdo(void);
extern u32 sub_unk3_RoyalCrypt_KeyBlock(void);
extern u32 sub_unk3_RoyalCrypt_KingGustaf(void);
extern u32 sub_unk3_RoyalCrypt_MushroomPit(void);
extern u32 sub_unk3_RoyalCrypt_WaterRope(void);
extern u32 sub_unk3_RoyalValleyGraves_Gina(void);
extern u32 sub_unk3_RoyalValleyGraves_HeartPiece(void);
extern u32 sub_unk3_RoyalValley_ForestMaze(void);
extern u32 sub_unk3_RoyalValley_Main(void);
extern u32 sub_unk3_Ruins_Armos(void);
extern u32 sub_unk3_Ruins_Beanstalk(void);
extern u32 sub_unk3_Ruins_Entrance(void);
extern u32 sub_unk3_Ruins_FortressEntrance(void);
extern u32 sub_unk3_Ruins_LadderToTektites(void);
extern u32 sub_unk3_Ruins_TripleTektites(void);
extern u32 sub_unk3_SanctuaryEntrance_Main(void);
extern u32 sub_unk3_Sanctuary_Hall(void);
extern u32 sub_unk3_Sanctuary_Main(void);
extern u32 sub_unk3_Sanctuary_StainedGlass(void);
extern u32 sub_unk3_SimonsSimulation_Main(void);
extern u32 sub_unk3_TempleOfDroplets_AfterMadderpillars(void);
extern u32 sub_unk3_TempleOfDroplets_BigBlueChuchu(void);
extern u32 sub_unk3_TempleOfDroplets_BigBlueChuchuKey(void);
extern u32 sub_unk3_TempleOfDroplets_BigOcto(void);
extern u32 sub_unk3_TempleOfDroplets_BlockCloneButtonPuzzle(void);
extern u32 sub_unk3_TempleOfDroplets_BlockCloneIceBridge(void);
extern u32 sub_unk3_TempleOfDroplets_BlockClonePuzzle(void);
extern u32 sub_unk3_TempleOfDroplets_BlueChuchuKeyLever(void);
extern u32 sub_unk3_TempleOfDroplets_BombWall(void);
extern u32 sub_unk3_TempleOfDroplets_BossKey(void);
extern u32 sub_unk3_TempleOfDroplets_CompassRoom(void);
extern u32 sub_unk3_TempleOfDroplets_EastHole(void);
extern u32 sub_unk3_TempleOfDroplets_Element(void);
extern u32 sub_unk3_TempleOfDroplets_Entrance(void);
extern u32 sub_unk3_TempleOfDroplets_FireBars(void);
extern u32 sub_unk3_TempleOfDroplets_FlameBarBlockPuzzle(void);
extern u32 sub_unk3_TempleOfDroplets_HoleToBlueChuchu(void);
extern u32 sub_unk3_TempleOfDroplets_IceCorner(void);
extern u32 sub_unk3_TempleOfDroplets_IcePitMaze(void);
extern u32 sub_unk3_TempleOfDroplets_LanternMaze(void);
extern u32 sub_unk3_TempleOfDroplets_LanternScissors(void);
extern u32 sub_unk3_TempleOfDroplets_Lanterns(void);
extern u32 sub_unk3_TempleOfDroplets_LilypadEastB2(void);
extern u32 sub_unk3_TempleOfDroplets_LilypadIceBlocks(void);
extern u32 sub_unk3_TempleOfDroplets_LilypadMiddleB2(void);
extern u32 sub_unk3_TempleOfDroplets_LilypadWestB2(void);
extern u32 sub_unk3_TempleOfDroplets_Madderpillars(void);
extern u32 sub_unk3_TempleOfDroplets_MulldozerKey(void);
extern u32 sub_unk3_TempleOfDroplets_NorthSmallKey(void);
extern u32 sub_unk3_TempleOfDroplets_NorthSplit(void);
extern u32 sub_unk3_TempleOfDroplets_NorthwestStairs(void);
extern u32 sub_unk3_TempleOfDroplets_Pit(void);
extern u32 sub_unk3_TempleOfDroplets_ScissorsMiniboss(void);
extern u32 sub_unk3_TempleOfDroplets_SpikeBar(void);
extern u32 sub_unk3_TempleOfDroplets_StairsToScissorsMiniboss(void);
extern u32 sub_unk3_TempleOfDroplets_ToBigBlueChuchu(void);
extern u32 sub_unk3_TempleOfDroplets_WaterfallNortheast(void);
extern u32 sub_unk3_TempleOfDroplets_WaterfallNorthwest(void);
extern u32 sub_unk3_TempleOfDroplets_WaterfallSoutheast(void);
extern u32 sub_unk3_TempleOfDroplets_WaterfallSouthwest(void);
extern u32 sub_unk3_TempleOfDroplets_WestHole(void);
extern u32 sub_unk3_TownMinishHoles_5(void);
extern u32 sub_unk3_TownMinishHoles_Cafe(void);
extern u32 sub_unk3_TownMinishHoles_Carpenter(void);
extern u32 sub_unk3_TownMinishHoles_DrLeft(void);
extern u32 sub_unk3_TownMinishHoles_LibrariBookHouse(void);
extern u32 sub_unk3_TownMinishHoles_LibraryBookshelf(void);
extern u32 sub_unk3_TownMinishHoles_MayorsHouse(void);
extern u32 sub_unk3_TownMinishHoles_RemShoeShop(void);
extern u32 sub_unk3_TownMinishHoles_WestOracle(void);
extern u32 sub_unk3_TreeInteriors_14(void);
extern u32 sub_unk3_TreeInteriors_1C(void);
extern u32 sub_unk3_TreeInteriors_1E(void);
extern u32 sub_unk3_TreeInteriors_BoomerangNortheast(void);
extern u32 sub_unk3_TreeInteriors_BoomerangNorthwest(void);
extern u32 sub_unk3_TreeInteriors_BoomerangSoutheast(void);
extern u32 sub_unk3_TreeInteriors_BoomerangSouthwest(void);
extern u32 sub_unk3_TreeInteriors_HeartPiece(void);
extern u32 sub_unk3_TreeInteriors_MinishWoodsBusinessScrub(void);
extern u32 sub_unk3_TreeInteriors_MinishWoodsGreatFairy(void);
extern u32 sub_unk3_TreeInteriors_NorthHyruleFieldFairyFountain(void);
extern u32 sub_unk3_TreeInteriors_PercysTreehouse(void);
extern u32 sub_unk3_TreeInteriors_StairsToCarlov(void);
extern u32 sub_unk3_TreeInteriors_UnusedHeartContainer(void);
extern u32 sub_unk3_TreeInteriors_Waveblade(void);
extern u32 sub_unk3_TreeInteriors_WesternWoodsHeartPiece(void);
extern u32 sub_unk3_TreeInteriors_WitchHut(void);
extern u32 sub_unk3_Vaati2_Main(void);
extern u32 sub_unk3_Vaati3_Main(void);
extern u32 sub_unk3_VaatisArms_First(void);
extern u32 sub_unk3_VaatisArms_Second(void);
extern u32 sub_unk3_VeilFallsCaves_BlockPuzzle(void);
extern u32 sub_unk3_VeilFallsCaves_Entrance(void);
extern u32 sub_unk3_VeilFallsCaves_Exit(void);
extern u32 sub_unk3_VeilFallsCaves_Hallway1F(void);
extern u32 sub_unk3_VeilFallsCaves_Hallway2F(void);
extern u32 sub_unk3_VeilFallsCaves_HeartPiece(void);
extern u32 sub_unk3_VeilFallsCaves_RupeePath(void);
extern u32 sub_unk3_VeilFallsCaves_SecretChest(void);
extern u32 sub_unk3_VeilFallsCaves_SecretRoom(void);
extern u32 sub_unk3_VeilFallsCaves_SecretStaircases(void);
extern u32 sub_unk3_VeilFallsDigCave_Main(void);
extern u32 sub_unk3_VeilFallsTop_Main(void);
extern u32 sub_unk3_VeilFalls_Main(void);
extern u32 sub_unk3_WindTribeTowerRoof_Main(void);
extern u32 sub_unk3_WindTribeTower_Entrance(void);
extern u32 sub_unk3_WindTribeTower_Floor2(void);
extern u32 sub_unk3_WindTribeTower_Floor3(void);
extern u32 sub_unk3_WindTribeTower_Floor4(void);

typedef struct {
    u16 area;
    u16 room;
    void* props[4]; /* properties 4, 5, 6, 7 */
} RoomFuncEntry;

static const RoomFuncEntry sRoomFuncTable[] = {
    { 0x00,
      0x00,
      { (void*)sub_unk1_MinishWoods_Main, NULL, (void*)sub_unk3_MinishWoods_Main,
        (void*)sub_StateChange_MinishWoods_Main } },
    { 0x01, 0x00, { NULL, NULL, (void*)sub_unk3_MinishVillage_Main, (void*)sub_StateChange_MinishVillage_Main } },
    { 0x01,
      0x01,
      { NULL, (void*)sub_unk2_MinishVillage_SideHouse, (void*)sub_unk3_MinishVillage_SideHouse,
        (void*)sub_StateChange_MinishVillage_SideHouse } },
    { 0x02, 0x00, { NULL, NULL, (void*)sub_unk3_HyruleTown_0, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x01, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x02, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x03, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x04, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x05, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x06, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x07, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x08, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x02, 0x09, { (void*)sub_unk1_HyruleTown_8, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x03,
      0x00,
      { NULL, NULL, (void*)sub_unk3_HyruleField_WesternWoodSouth,
        (void*)sub_StateChange_HyruleField_WesternWoodSouth } },
    { 0x03,
      0x01,
      { (void*)sub_unk1_HyruleField_SouthHyruleField, NULL, (void*)sub_unk3_HyruleField_SouthHyruleField,
        (void*)sub_StateChange_HyruleField_SouthHyruleField } },
    { 0x03,
      0x02,
      { NULL, NULL, (void*)sub_unk3_HyruleField_EasternHillsSouth,
        (void*)sub_StateChange_HyruleField_EasternHillsSouth } },
    { 0x03,
      0x03,
      { NULL, NULL, (void*)sub_unk3_HyruleField_EasternHillsCenter,
        (void*)sub_StateChange_HyruleField_EasternHillsCenter } },
    { 0x03,
      0x04,
      { NULL, NULL, (void*)sub_unk3_HyruleField_EasternHillsNorth,
        (void*)sub_StateChange_HyruleField_EasternHillsNorth } },
    { 0x03,
      0x05,
      { NULL, NULL, (void*)sub_unk3_HyruleField_LonLonRanch, (void*)sub_StateChange_HyruleField_LonLonRanch } },
    { 0x03,
      0x06,
      { NULL, NULL, (void*)sub_unk3_HyruleField_OutsideCastle, (void*)sub_StateChange_HyruleField_OutsideCastle } },
    { 0x03,
      0x07,
      { NULL, NULL, (void*)sub_unk3_HyruleField_TrilbyHighlands, (void*)sub_StateChange_HyruleField_TrilbyHighlands } },
    { 0x03,
      0x08,
      { NULL, NULL, (void*)sub_unk3_HyruleField_WesternWoodsNorth,
        (void*)sub_StateChange_HyruleField_WesternWoodsNorth } },
    { 0x03,
      0x09,
      { NULL, NULL, (void*)sub_unk3_HyruleField_WesternWoodsCenter,
        (void*)sub_StateChange_HyruleField_WesternWoodsCenter } },
    { 0x04,
      0x00,
      { (void*)sub_unk1_CastorWilds_Main, NULL, (void*)sub_unk3_CastorWilds_Main,
        (void*)sub_StateChange_CastorWilds_Main } },
    { 0x05, 0x00, { NULL, NULL, (void*)sub_unk3_Ruins_Entrance, (void*)sub_StateChange_Ruins_Entrance } },
    { 0x05, 0x01, { NULL, NULL, (void*)sub_unk3_Ruins_Beanstalk, (void*)sub_StateChange_Ruins_Beanstalk } },
    { 0x05,
      0x02,
      { (void*)sub_unk1_Ruins_TripleTektites, NULL, (void*)sub_unk3_Ruins_TripleTektites,
        (void*)sub_StateChange_Ruins_TripleTektites } },
    { 0x05,
      0x03,
      { NULL, NULL, (void*)sub_unk3_Ruins_LadderToTektites, (void*)sub_StateChange_Ruins_LadderToTektites } },
    { 0x05,
      0x04,
      { NULL, NULL, (void*)sub_unk3_Ruins_FortressEntrance, (void*)sub_StateChange_Ruins_FortressEntrance } },
    { 0x05, 0x05, { NULL, NULL, (void*)sub_unk3_Ruins_Armos, (void*)sub_StateChange_Ruins_Armos } },
    { 0x06,
      0x00,
      { (void*)sub_unk1_MtCrenel_MountainTop, NULL, (void*)sub_unk3_MtCrenel_MountainTop,
        (void*)sub_StateChange_MtCrenel_MountainTop } },
    { 0x06, 0x01, { NULL, NULL, (void*)sub_unk3_MtCrenel_WallClimb, (void*)sub_StateChange_MtCrenel_WallClimb } },
    { 0x06,
      0x02,
      { (void*)sub_unk1_MtCrenel_CaveOfFlamesEntrance, NULL, (void*)sub_unk3_MtCrenel_CaveOfFlamesEntrance,
        (void*)sub_StateChange_MtCrenel_CaveOfFlamesEntrance } },
    { 0x06,
      0x03,
      { NULL, NULL, (void*)sub_unk3_MtCrenel_GustJarShortcut, (void*)sub_StateChange_MtCrenel_GustJarShortcut } },
    { 0x06, 0x04, { NULL, NULL, (void*)sub_unk3_MtCrenel_Entrance, (void*)sub_StateChange_MtCrenel_Entrance } },
    { 0x07, 0x00, { NULL, NULL, (void*)sub_unk3_CastleGarden_Main, (void*)sub_StateChange_CastleGarden_Main } },
    { 0x08,
      0x00,
      { (void*)sub_unk1_CloudTops_House, NULL, (void*)sub_unk3_CloudTops_House,
        (void*)sub_StateChange_CloudTops_House } },
    { 0x08, 0x01, { NULL, NULL, (void*)sub_unk3_CloudTops_Middle, (void*)sub_StateChange_CloudTops_Middle } },
    { 0x08, 0x02, { NULL, NULL, (void*)sub_unk3_CloudTops_Bottom, (void*)sub_StateChange_CloudTops_Bottom } },
    { 0x09, 0x00, { NULL, NULL, (void*)sub_unk3_RoyalValley_Main, (void*)sub_StateChange_RoyalValley_Main } },
    { 0x09,
      0x01,
      { NULL, NULL, (void*)sub_unk3_RoyalValley_ForestMaze, (void*)sub_StateChange_RoyalValley_ForestMaze } },
    { 0x0A,
      0x00,
      { (void*)sub_unk1_VeilFalls_Main, NULL, (void*)sub_unk3_VeilFalls_Main, (void*)sub_StateChange_VeilFalls_Main } },
    { 0x0B, 0x00, { NULL, NULL, (void*)sub_unk3_LakeHylia_Main, (void*)sub_StateChange_LakeHylia_Main } },
    { 0x0B, 0x01, { NULL, NULL, (void*)sub_unk3_LakeHylia_Beanstalk, (void*)sub_StateChange_Ruins_LadderToTektites1 } },
    { 0x0C, 0x00, { NULL, NULL, (void*)sub_unk3_LakeWoodsCave_Main, (void*)sub_StateChange_Ruins_LadderToTektites2 } },
    { 0x0D,
      0x00,
      { NULL, NULL, (void*)sub_unk3_Beanstalks_MountCrenel, (void*)sub_StateChange_Beanstalks_MountCrenel } },
    { 0x0D, 0x01, { NULL, NULL, (void*)sub_unk3_Beanstalks_LakeHylia, (void*)sub_StateChange_Beanstalks_LakeHylia } },
    { 0x0D, 0x02, { NULL, NULL, (void*)sub_unk3_Beanstalks_Ruins, (void*)sub_StateChange_Beanstalks_Ruins } },
    { 0x0D,
      0x03,
      { NULL, NULL, (void*)sub_unk3_Beanstalks_EasternHills, (void*)sub_StateChange_Beanstalks_EasternHills } },
    { 0x0D,
      0x04,
      { NULL, NULL, (void*)sub_unk3_Beanstalks_WesternWoods, (void*)sub_StateChange_Beanstalks_WesternWoods } },
    { 0x0D,
      0x10,
      { NULL, NULL, (void*)sub_unk3_Beanstalks_MountCrenelClimb, (void*)sub_StateChange_Beanstalks_MountCrenelClimb } },
    { 0x0D,
      0x11,
      { NULL, NULL, (void*)sub_unk3_Beanstalks_LakeHyliaClimb, (void*)sub_StateChange_Beanstalks_LakeHyliaClimb } },
    { 0x0D, 0x12, { NULL, NULL, (void*)sub_unk3_Beanstalks_RuinsClimb, (void*)sub_StateChange_Beanstalks_RuinsClimb } },
    { 0x0D,
      0x13,
      { NULL, NULL, (void*)sub_unk3_Beanstalks_EasternHillsClimb,
        (void*)sub_StateChange_Beanstalks_EasternHillsClimb } },
    { 0x0D,
      0x14,
      { NULL, NULL, (void*)sub_unk3_Beanstalks_WesternWoodsClimb,
        (void*)sub_StateChange_Beanstalks_WesternWoodsClimb } },
    { 0x0E, 0x00, { NULL, NULL, (void*)sub_unk3_Empty_Main, (void*)sub_StateChange_Empty_Main } },
    { 0x0F, 0x00, { NULL, NULL, (void*)sub_unk3_HyruleDigCaves_Main, (void*)sub_StateChange_Ruins_LadderToTektites6 } },
    { 0x10, 0x00, { NULL, NULL, (void*)sub_unk3_MelarisMine_Main, (void*)sub_StateChange_MelarisMine_Main } },
    { 0x11,
      0x00,
      { (void*)sub_unk1_MinishPaths_ToMinishVillage, NULL, (void*)sub_unk3_MinishPaths_ToMinishVillage,
        (void*)sub_StateChange_MinishPaths_ToMinishVillage } },
    { 0x11,
      0x01,
      { (void*)sub_unk1_MinishPaths_CastorWilds, NULL, (void*)sub_unk3_MinishPaths_CastorWilds,
        (void*)sub_StateChange_MinishPaths_CastorWilds } },
    { 0x11,
      0x02,
      { (void*)sub_unk1_MinishPaths_HyruleTown, NULL, (void*)sub_unk3_MinishPaths_HyruleTown,
        (void*)sub_StateChange_MinishPaths_HyruleTown } },
    { 0x11,
      0x03,
      { (void*)sub_unk1_MinishPaths_LonLonRanch, NULL, (void*)sub_unk3_MinishPaths_LonLonRanch,
        (void*)sub_StateChange_MinishPaths_LonLonRanch } },
    { 0x11,
      0x04,
      { (void*)sub_unk1_MinishPaths_MayorsCabin, NULL, (void*)sub_unk3_MinishPaths_MayorsCabin,
        (void*)sub_StateChange_MinishPaths_MayorsCabin } },
    { 0x12,
      0x00,
      { NULL, NULL, (void*)sub_unk3_CrenelMinishPaths_CrenelBean,
        (void*)sub_StateChange_CrenelMinishPaths_CrenelBean } },
    { 0x12,
      0x01,
      { (void*)sub_unk1_CrenelMinishPaths_CrenelWater, NULL, (void*)sub_unk3_CrenelMinishPaths_CrenelWater,
        (void*)sub_StateChange_CrenelMinishPaths_CrenelWater } },
    { 0x12,
      0x02,
      { (void*)sub_unk1_CrenelMinishPaths_Rainfall, NULL, (void*)sub_unk3_CrenelMinishPaths_Rainfall,
        (void*)sub_StateChange_CrenelMinishPaths_Rainfall } },
    { 0x12,
      0x03,
      { (void*)sub_unk1_CrenelMinishPaths_MelarisMine, NULL, (void*)sub_unk3_CrenelMinishPaths_MelarisMine,
        (void*)sub_StateChange_CrenelMinishPaths_MelarisMine } },
    { 0x13,
      0x00,
      { NULL, NULL, (void*)sub_unk3_DigCaves1_HyruleFieldFarm, (void*)sub_StateChange_Ruins_LadderToTektites8 } },
    { 0x13,
      0x03,
      { NULL, NULL, (void*)sub_unk3_DigCaves1_TrilbyHighlands, (void*)sub_StateChange_Ruins_LadderToTektites9 } },
    { 0x14, 0x00, { NULL, NULL, (void*)sub_unk3_CrenelDigCave_Main, (void*)sub_StateChange_Ruins_LadderToTektites7 } },
    { 0x15, 0x00, { NULL, NULL, NULL, (void*)sub_StateChange_HyruleTown_0 } },
    { 0x16, 0x00, { NULL, NULL, (void*)sub_unk3_VeilFallsDigCave_Main, (void*)sub_StateChange_VeilFallsDigCave_Main } },
    { 0x17,
      0x00,
      { NULL, NULL, (void*)sub_unk3_CastorWildsDigCave_Main, (void*)sub_StateChange_Ruins_LadderToTektites5 } },
    { 0x18,
      0x00,
      { NULL, NULL, (void*)sub_unk3_OuterFortressOfWinds_EntranceHall,
        (void*)sub_StateChange_OuterFortressOfWinds_EntranceHall } },
    { 0x18,
      0x01,
      { NULL, NULL, (void*)sub_unk3_OuterFortressOfWinds_2F, (void*)sub_StateChange_OuterFortressOfWinds_2F } },
    { 0x18,
      0x02,
      { NULL, NULL, (void*)sub_unk3_OuterFortressOfWinds_3F, (void*)sub_StateChange_OuterFortressOfWinds_3F } },
    { 0x18,
      0x03,
      { NULL, NULL, (void*)sub_unk3_OuterFortressOfWinds_MoleMitts,
        (void*)sub_StateChange_OuterFortressOfWinds_MoleMitts } },
    { 0x18,
      0x04,
      { NULL, NULL, (void*)sub_unk3_OuterFortressOfWinds_SmallKey,
        (void*)sub_StateChange_OuterFortressOfWinds_SmallKey } },
    { 0x19,
      0x00,
      { NULL, NULL, (void*)sub_unk3_HyliaDigCaves_Middle, (void*)sub_StateChange_Ruins_LadderToTektites3 } },
    { 0x19, 0x01, { NULL, NULL, (void*)sub_unk3_HyliaDigCaves_North, (void*)sub_StateChange_Ruins_LadderToTektites4 } },
    { 0x1A, 0x00, { NULL, NULL, (void*)sub_unk3_VeilFallsTop_Main, (void*)sub_StateChange_VeilFallsTop_Main } },
    { 0x20,
      0x00,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_GentariMain,
        (void*)sub_StateChange_MinishHouseInteriors_GentariMain } },
    { 0x20,
      0x01,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_GentariExit,
        (void*)sub_StateChange_MinishHouseInteriors_GentariExit } },
    { 0x20,
      0x02,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_Festari,
        (void*)sub_StateChange_MinishHouseInteriors_Festari } },
    { 0x20,
      0x03,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_Red, (void*)sub_StateChange_MinishHouseInteriors_Red } },
    { 0x20,
      0x04,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_Green, (void*)sub_StateChange_MinishHouseInteriors_Green } },
    { 0x20,
      0x05,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_Blue, (void*)sub_StateChange_MinishHouseInteriors_Blue } },
    { 0x20,
      0x06,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_SideArea,
        (void*)sub_StateChange_MinishHouseInteriors_SideArea } },
    { 0x20,
      0x07,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_ShoeMinish,
        (void*)sub_StateChange_MinishHouseInteriors_ShoeMinish } },
    { 0x20,
      0x08,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_PotMinish,
        (void*)sub_StateChange_MinishHouseInteriors_PotMinish } },
    { 0x20,
      0x09,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_BarrelMinish,
        (void*)sub_StateChange_MinishHouseInteriors_BarrelMinish } },
    { 0x20,
      0x10,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_MelariMinesSouthwest,
        (void*)sub_StateChange_MinishHouseInteriors_MelariMinesSouthwest } },
    { 0x20,
      0x11,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_MelariMinesSoutheast,
        (void*)sub_StateChange_MinishHouseInteriors_MelariMinesSoutheast } },
    { 0x20,
      0x12,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_MelariMinesEast,
        (void*)sub_StateChange_MinishHouseInteriors_MelariMinesEast } },
    { 0x20,
      0x20,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_HyruleFieldSouthwest,
        (void*)sub_StateChange_MinishHouseInteriors_HyruleFieldSouthwest } },
    { 0x20,
      0x21,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_SouthHyruleField,
        (void*)sub_StateChange_MinishHouseInteriors_SouthHyruleField } },
    { 0x20,
      0x22,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_NextToKnuckle,
        (void*)sub_StateChange_MinishHouseInteriors_NextToKnuckle } },
    { 0x20,
      0x23,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_Librari,
        (void*)sub_StateChange_MinishHouseInteriors_Librari } },
    { 0x20,
      0x24,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_HyruleFieldExit,
        (void*)sub_StateChange_MinishHouseInteriors_HyruleFieldExit } },
    { 0x20,
      0x25,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_HyruleTown,
        (void*)sub_StateChange_MinishHouseInteriors_HyruleTown } },
    { 0x20,
      0x26,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_MinishWoodsBomb,
        (void*)sub_StateChange_MinishHouseInteriors_MinishWoodsBomb } },
    { 0x20,
      0x27,
      { NULL, NULL, (void*)sub_unk3_MinishHouseInteriors_LakeHyliaOcarina,
        (void*)sub_StateChange_MinishHouseInteriors_LakeHyliaOcarina } },
    { 0x21, 0x00, { NULL, NULL, (void*)sub_unk3_HouseInteriors1_Mayor, (void*)sub_StateChange_HouseInteriors1_Mayor } },
    { 0x21,
      0x01,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_PostOffice, (void*)sub_StateChange_HouseInteriors1_PostOffice } },
    { 0x21,
      0x02,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_Library2F, (void*)sub_StateChange_HouseInteriors1_Library2F } },
    { 0x21,
      0x03,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_Library1F, (void*)sub_StateChange_HouseInteriors1_Library1F } },
    { 0x21, 0x04, { NULL, NULL, (void*)sub_unk3_HouseInteriors1_Inn1F, (void*)sub_StateChange_HouseInteriors1_Inn1F } },
    { 0x21,
      0x05,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_InnWestRoom, (void*)sub_StateChange_HouseInteriors1_InnWestRoom } },
    { 0x21,
      0x06,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_InnMiddleRoom,
        (void*)sub_StateChange_HouseInteriors1_InnMiddleRoom } },
    { 0x21,
      0x07,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_InnEastRoom, (void*)sub_StateChange_HouseInteriors1_InnEastRoom } },
    { 0x21,
      0x08,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_InnWest2F, (void*)sub_StateChange_HouseInteriors1_InnWest2F } },
    { 0x21,
      0x09,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_InnEast2F, (void*)sub_StateChange_HouseInteriors1_InnEast2F } },
    { 0x21,
      0x0A,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_InnMinishHeartPiece,
        (void*)sub_StateChange_HouseInteriors1_InnMinishHeartPiece } },
    { 0x21,
      0x0B,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_SchoolWest, (void*)sub_StateChange_HouseInteriors1_SchoolWest } },
    { 0x21,
      0x0C,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors1_SchoolEast, (void*)sub_StateChange_HouseInteriors1_SchoolEast } },
    { 0x22,
      0x00,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_Stranger, (void*)sub_StateChange_HouseInteriors2_Stranger } },
    { 0x22,
      0x01,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_WestOracle, (void*)sub_StateChange_HouseInteriors2_WestOracle } },
    { 0x22, 0x02, { NULL, NULL, (void*)sub_unk3_HouseInteriors2_2, (void*)sub_StateChange_Ruins_Beanstalk5 } },
    { 0x22, 0x03, { NULL, NULL, (void*)sub_unk3_HouseInteriors2_3, (void*)sub_StateChange_Ruins_Beanstalk6 } },
    { 0x22,
      0x04,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_DrLeft, (void*)sub_StateChange_HouseInteriors2_DrLeft } },
    { 0x22, 0x06, { NULL, NULL, (void*)sub_unk3_HouseInteriors2_Romio, (void*)sub_StateChange_HouseInteriors2_Romio } },
    { 0x22,
      0x07,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_Julietta, (void*)sub_StateChange_HouseInteriors2_Julietta } },
    { 0x22, 0x08, { NULL, NULL, (void*)sub_unk3_HouseInteriors2_Percy, (void*)sub_StateChange_HouseInteriors2_Percy } },
    { 0x22,
      0x09,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_EastOracle, (void*)sub_StateChange_HouseInteriors2_EastOracle } },
    { 0x22, 0x0A, { NULL, NULL, (void*)sub_unk3_HouseInteriors2_A, (void*)sub_StateChange_Ruins_Beanstalk7 } },
    { 0x22, 0x0B, { NULL, NULL, (void*)sub_unk3_HouseInteriors2_B, (void*)sub_StateChange_Ruins_Beanstalk8 } },
    { 0x22, 0x0C, { NULL, NULL, (void*)sub_unk3_HouseInteriors2_Cucco, (void*)sub_StateChange_Ruins_Beanstalk9 } },
    { 0x22,
      0x10,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_LinksHouseEntrance,
        (void*)sub_StateChange_HouseInteriors2_LinksHouseEntrance } },
    { 0x22,
      0x11,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_LinksHouseSmith,
        (void*)sub_StateChange_HouseInteriors2_LinksHouseSmith } },
    { 0x22, 0x12, { NULL, NULL, (void*)sub_unk3_HouseInteriors2_Dampe, (void*)sub_StateChange_HouseInteriors2_Dampe } },
    { 0x22,
      0x14,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_StockwellLakeHouse,
        (void*)sub_StateChange_HouseInteriors2_StockwellLakeHouse } },
    { 0x22,
      0x15,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors2_LinksHouseBedroom,
        (void*)sub_StateChange_HouseInteriors2_LinksHouseBedroom } },
    { 0x23,
      0x00,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors3_StockwellShop,
        (void*)sub_StateChange_HouseInteriors3_StockwellShop } },
    { 0x23, 0x01, { NULL, NULL, (void*)sub_unk3_HouseInteriors3_Cafe, (void*)sub_StateChange_HouseInteriors3_Cafe } },
    { 0x23,
      0x02,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors3_RemShoeShop, (void*)sub_StateChange_HouseInteriors3_RemShoeShop } },
    { 0x23,
      0x03,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors3_Bakery, (void*)sub_StateChange_HouseInteriors3_Bakery } },
    { 0x23, 0x04, { NULL, NULL, (void*)sub_unk3_HouseInteriors3_Simon, (void*)sub_StateChange_HouseInteriors3_Simon } },
    { 0x23,
      0x05,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors3_FigurineHouse,
        (void*)sub_StateChange_HouseInteriors3_FigurineHouse } },
    { 0x23,
      0x06,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors3_BorlovEntrance,
        (void*)sub_StateChange_HouseInteriors3_BorlovEntrance } },
    { 0x23,
      0x07,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors3_Carlov, (void*)sub_StateChange_HouseInteriors3_Carlov } },
    { 0x23,
      0x08,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors3_Borlov, (void*)sub_StateChange_HouseInteriors3_Borlov } },
    { 0x24,
      0x00,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_WitchHut, (void*)sub_StateChange_TreeInteriors_WitchHut } },
    { 0x24,
      0x10,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_StairsToCarlov,
        (void*)sub_StateChange_TreeInteriors_StairsToCarlov } },
    { 0x24,
      0x11,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_PercysTreehouse,
        (void*)sub_StateChange_TreeInteriors_PercysTreehouse } },
    { 0x24,
      0x12,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_HeartPiece, (void*)sub_StateChange_TreeInteriors_HeartPiece } },
    { 0x24,
      0x13,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_Waveblade, (void*)sub_StateChange_TreeInteriors_Waveblade } },
    { 0x24, 0x14, { NULL, NULL, (void*)sub_unk3_TreeInteriors_14, (void*)sub_StateChange_TreeInteriors_14 } },
    { 0x24,
      0x15,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_BoomerangNorthwest,
        (void*)sub_StateChange_TreeInteriors_BoomerangNorthwest } },
    { 0x24,
      0x16,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_BoomerangNortheast,
        (void*)sub_StateChange_TreeInteriors_BoomerangNortheast } },
    { 0x24,
      0x17,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_BoomerangSouthwest,
        (void*)sub_StateChange_TreeInteriors_BoomerangSouthwest } },
    { 0x24,
      0x18,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_BoomerangSoutheast,
        (void*)sub_StateChange_TreeInteriors_BoomerangSoutheast } },
    { 0x24,
      0x19,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_WesternWoodsHeartPiece,
        (void*)sub_StateChange_TreeInteriors_WesternWoodsHeartPiece } },
    { 0x24,
      0x1A,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_NorthHyruleFieldFairyFountain,
        (void*)sub_StateChange_TreeInteriors_NorthHyruleFieldFairyFountain } },
    { 0x24,
      0x1B,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_MinishWoodsGreatFairy,
        (void*)sub_StateChange_TreeInteriors_MinishWoodsGreatFairy } },
    { 0x24, 0x1C, { NULL, NULL, (void*)sub_unk3_TreeInteriors_1C, (void*)sub_StateChange_TreeInteriors_1C } },
    { 0x24,
      0x1D,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_MinishWoodsBusinessScrub,
        (void*)sub_StateChange_TreeInteriors_MinishWoodsBusinessScrub } },
    { 0x24, 0x1E, { NULL, NULL, (void*)sub_unk3_TreeInteriors_1E, (void*)sub_StateChange_TreeInteriors_1E } },
    { 0x24,
      0x1F,
      { NULL, NULL, (void*)sub_unk3_TreeInteriors_UnusedHeartContainer,
        (void*)sub_StateChange_TreeInteriors_UnusedHeartContainer } },
    { 0x25, 0x00, { NULL, NULL, (void*)sub_unk3_Dojos_Grayblade, (void*)sub_StateChange_Dojos_Grayblade } },
    { 0x25, 0x01, { NULL, NULL, (void*)sub_unk3_Dojos_Splitblade, (void*)sub_StateChange_Dojos_Splitblade } },
    { 0x25, 0x02, { NULL, NULL, (void*)sub_unk3_Dojos_Greatblade, (void*)sub_StateChange_Dojos_Greatblade } },
    { 0x25, 0x03, { NULL, NULL, (void*)sub_unk3_Dojos_Scarblade, (void*)sub_StateChange_Dojos_Scarblade } },
    { 0x25, 0x04, { NULL, NULL, (void*)sub_unk3_Dojos_Swiftblade, (void*)sub_StateChange_Dojos_Swiftblade } },
    { 0x25, 0x05, { NULL, NULL, (void*)sub_unk3_Dojos_Grimblade, (void*)sub_StateChange_Dojos_Grimblade } },
    { 0x25, 0x06, { NULL, NULL, (void*)sub_unk3_Dojos_Waveblade, (void*)sub_StateChange_Dojos_Waveblade } },
    { 0x25, 0x07, { NULL, NULL, (void*)sub_unk3_Dojos_7, (void*)sub_StateChange_Dojos_7 } },
    { 0x25, 0x08, { NULL, NULL, (void*)sub_unk3_Dojos_8, (void*)sub_StateChange_Dojos_8 } },
    { 0x25, 0x09, { NULL, NULL, (void*)sub_unk3_Dojos_9, (void*)sub_StateChange_Dojos_9 } },
    { 0x25, 0x0A, { NULL, NULL, (void*)sub_unk3_Dojos_ToGrimblade, (void*)sub_StateChange_Dojos_ToGrimblade } },
    { 0x25, 0x0B, { NULL, NULL, (void*)sub_unk3_Dojos_ToSplitblade, (void*)sub_StateChange_Dojos_ToSplitblade } },
    { 0x25, 0x0C, { NULL, NULL, (void*)sub_unk3_Dojos_ToGreatblade, (void*)sub_StateChange_Dojos_ToGreatblade } },
    { 0x25, 0x0D, { NULL, NULL, (void*)sub_unk3_Dojos_ToScarblade, (void*)sub_StateChange_Dojos_ToScarblade } },
    { 0x26,
      0x00,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_BlockPushing, (void*)sub_StateChange_CrenelCaves_BlockPushing } },
    { 0x26,
      0x01,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_PillarCave, (void*)sub_StateChange_CrenelCaves_PillarCave } },
    { 0x26,
      0x02,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_BridgeSwitch, (void*)sub_StateChange_CrenelCaves_BridgeSwitch } },
    { 0x26,
      0x03,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_ExitToMines, (void*)sub_StateChange_CrenelCaves_ExitToMines } },
    { 0x26, 0x04, { NULL, NULL, (void*)sub_unk3_CrenelCaves_GripRing, (void*)sub_StateChange_CrenelCaves_GripRing } },
    { 0x26,
      0x05,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_FairyFountain, (void*)sub_StateChange_CrenelCaves_FairyFountain } },
    { 0x26,
      0x06,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_SpinyChuPuzzle, (void*)sub_StateChange_CrenelCaves_SpinyChuPuzzle } },
    { 0x26,
      0x07,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_ChuchuPotChest, (void*)sub_StateChange_CrenelCaves_ChuchuPotChest } },
    { 0x26,
      0x08,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_WaterHeartPiece, (void*)sub_StateChange_CrenelCaves_WaterHeartPiece } },
    { 0x26,
      0x09,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_RupeeFairyFountain,
        (void*)sub_StateChange_CrenelCaves_RupeeFairyFountain } },
    { 0x26,
      0x0A,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_HelmasaurHallway,
        (void*)sub_StateChange_CrenelCaves_HelmasaurHallway } },
    { 0x26,
      0x0B,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_MushroomKeese, (void*)sub_StateChange_CrenelCaves_MushroomKeese } },
    { 0x26,
      0x0C,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_LadderToSpringWater,
        (void*)sub_StateChange_CrenelCaves_LadderToSpringWater } },
    { 0x26,
      0x0D,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_BombBusinessScrub,
        (void*)sub_StateChange_CrenelCaves_BombBusinessScrub } },
    { 0x26, 0x0E, { NULL, NULL, (void*)sub_unk3_CrenelCaves_Hermit, (void*)sub_StateChange_CrenelCaves_Hermit } },
    { 0x26, 0x0F, { NULL, NULL, (void*)sub_unk3_CrenelCaves_HintScrub, (void*)sub_StateChange_CrenelCaves_HintScrub } },
    { 0x26,
      0x10,
      { NULL, NULL, (void*)sub_unk3_CrenelCaves_ToGrayblade, (void*)sub_StateChange_CrenelCaves_ToGrayblade } },
    { 0x27,
      0x00,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_LonLonRanchNorth,
        (void*)sub_StateChange_MinishCracks_LonLonRanchNorth } },
    { 0x27,
      0x01,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_LakeHyliaEast, (void*)sub_StateChange_MinishCracks_LakeHyliaEast } },
    { 0x27,
      0x02,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_HyruleCastleGarden,
        (void*)sub_StateChange_MinishCracks_HyruleCastleGarden } },
    { 0x27, 0x03, { NULL, NULL, (void*)sub_unk3_MinishCracks_MtCrenel, (void*)sub_StateChange_MinishCracks_MtCrenel } },
    { 0x27,
      0x04,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_EastHyruleCastle,
        (void*)sub_StateChange_MinishCracks_EastHyruleCastle } },
    { 0x27, 0x05, { NULL, NULL, (void*)sub_unk3_MinishCracks_5, (void*)sub_StateChange_MinishCracks_5 } },
    { 0x27,
      0x06,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_CastorWildsBow, (void*)sub_StateChange_MinishCracks_CastorWildsBow } },
    { 0x27,
      0x07,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_RuinsEntrance, (void*)sub_StateChange_MinishCracks_RuinsEntrance } },
    { 0x27,
      0x08,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_MinishWoodsSouth,
        (void*)sub_StateChange_MinishCracks_MinishWoodsSouth } },
    { 0x27,
      0x09,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_CastorWildsNorth,
        (void*)sub_StateChange_MinishCracks_CastorWildsNorth } },
    { 0x27,
      0x0A,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_CastorWildsWest,
        (void*)sub_StateChange_MinishCracks_CastorWildsWest } },
    { 0x27,
      0x0B,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_CastorWildsMiddle,
        (void*)sub_StateChange_MinishCracks_CastorWildsMiddle } },
    { 0x27,
      0x0C,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_RuinsTektite, (void*)sub_StateChange_MinishCracks_RuinsTektite } },
    { 0x27,
      0x0D,
      { NULL, NULL, (void*)sub_unk3_MinishCracks_CastorWildsNextToBow,
        (void*)sub_StateChange_MinishCracks_CastorWildsNextToBow } },
    { 0x27, 0x0E, { NULL, NULL, (void*)sub_unk3_MinishCracks_E, (void*)sub_StateChange_MinishCracks_E } },
    { 0x27, 0x0F, { NULL, NULL, (void*)sub_unk3_MinishCracks_F, (void*)sub_StateChange_MinishCracks_F } },
    { 0x27, 0x10, { NULL, NULL, (void*)sub_unk3_MinishCracks_10, (void*)sub_StateChange_MinishCracks_10 } },
    { 0x27, 0x11, { NULL, NULL, (void*)sub_unk3_MinishCracks_11, (void*)sub_StateChange_MinishCracks_11 } },
    { 0x28,
      0x00,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors4_Carpenter, (void*)sub_StateChange_HouseInteriors4_Carpenter } },
    { 0x28,
      0x01,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors4_Swiftblade, (void*)sub_StateChange_HouseInteriors4_Swiftblade } },
    { 0x28,
      0x02,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors4_RanchHouseWest,
        (void*)sub_StateChange_HouseInteriors4_RanchHouseWest } },
    { 0x28,
      0x03,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors4_RanchHouseEast,
        (void*)sub_StateChange_HouseInteriors4_RanchHouseEast } },
    { 0x28,
      0x04,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors4_FarmHouse, (void*)sub_StateChange_Ruins_LadderToTektites0 } },
    { 0x28,
      0x05,
      { NULL, NULL, (void*)sub_unk3_HouseInteriors4_MayorLakeCabin,
        (void*)sub_StateChange_HouseInteriors4_MayorLakeCabin } },
    { 0x29,
      0x00,
      { NULL, NULL, (void*)sub_unk3_GreatFairies_Graveyard, (void*)sub_StateChange_GreatFairies_Graveyard } },
    { 0x29,
      0x01,
      { NULL, NULL, (void*)sub_unk3_GreatFairies_MinishWoods, (void*)sub_StateChange_GreatFairies_MinishWoods } },
    { 0x29, 0x02, { NULL, NULL, (void*)sub_unk3_GreatFairies_MtCrenel, (void*)sub_StateChange_GreatFairies_MtCrenel } },
    { 0x2A, 0x00, { NULL, NULL, (void*)sub_unk3_CastorCaves_South, (void*)sub_StateChange_CastorCaves_South } },
    { 0x2A, 0x01, { NULL, NULL, (void*)sub_unk3_CastorCaves_North, (void*)sub_StateChange_CastorCaves_North } },
    { 0x2A, 0x02, { NULL, NULL, (void*)sub_unk3_CastorCaves_WindRuins, (void*)sub_StateChange_CastorCaves_WindRuins } },
    { 0x2A, 0x03, { NULL, NULL, (void*)sub_unk3_CastorCaves_Darknut, (void*)sub_StateChange_CastorCaves_Darknut } },
    { 0x2A,
      0x04,
      { NULL, NULL, (void*)sub_unk3_CastorCaves_HeartPiece, (void*)sub_StateChange_CastorCaves_HeartPiece } },
    { 0x2B, 0x00, { NULL, NULL, (void*)sub_unk3_CastorDarknut_Main, (void*)sub_StateChange_CastorDarknut_Main } },
    { 0x2B, 0x01, { NULL, NULL, (void*)sub_unk3_CastorDarknut_Hall, (void*)sub_StateChange_CastorDarknut_Hall } },
    { 0x2C,
      0x00,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_RuinsEntranceNorth,
        (void*)sub_StateChange_ArmosInteriors_RuinsEntranceNorth } },
    { 0x2C,
      0x01,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_RuinsEntranceSouth,
        (void*)sub_StateChange_ArmosInteriors_RuinsEntranceSouth } },
    { 0x2C,
      0x02,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_RuinsLeft, (void*)sub_StateChange_ArmosInteriors_RuinsLeft } },
    { 0x2C,
      0x03,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_RuinsMiddleLeft,
        (void*)sub_StateChange_ArmosInteriors_RuinsMiddleLeft } },
    { 0x2C,
      0x04,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_RuinsMiddleRight,
        (void*)sub_StateChange_ArmosInteriors_RuinsMiddleRight } },
    { 0x2C,
      0x05,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_RuinsRight, (void*)sub_StateChange_ArmosInteriors_RuinsRight } },
    { 0x2C, 0x06, { NULL, NULL, (void*)sub_unk3_ArmosInteriors_6, (void*)sub_StateChange_ArmosInteriors_6 } },
    { 0x2C,
      0x07,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_RuinsGrassPath,
        (void*)sub_StateChange_ArmosInteriors_RuinsGrassPath } },
    { 0x2C, 0x08, { NULL, NULL, (void*)sub_unk3_ArmosInteriors_8, (void*)sub_StateChange_ArmosInteriors_8 } },
    { 0x2C,
      0x09,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_FortressOfWindsLeft,
        (void*)sub_StateChange_ArmosInteriors_FortressOfWindsLeft } },
    { 0x2C,
      0x0A,
      { NULL, NULL, (void*)sub_unk3_ArmosInteriors_FortressOfWindsRight,
        (void*)sub_StateChange_ArmosInteriors_FortressOfWindsRight } },
    { 0x2D,
      0x00,
      { NULL, NULL, (void*)sub_unk3_TownMinishHoles_MayorsHouse, (void*)sub_StateChange_TownMinishHoles_MayorsHouse } },
    { 0x2D,
      0x01,
      { NULL, NULL, (void*)sub_unk3_TownMinishHoles_WestOracle, (void*)sub_StateChange_TownMinishHoles_WestOracle } },
    { 0x2D,
      0x02,
      { NULL, NULL, (void*)sub_unk3_TownMinishHoles_DrLeft, (void*)sub_StateChange_TownMinishHoles_DrLeft } },
    { 0x2D,
      0x03,
      { NULL, NULL, (void*)sub_unk3_TownMinishHoles_Carpenter, (void*)sub_StateChange_TownMinishHoles_Carpenter } },
    { 0x2D, 0x04, { NULL, NULL, (void*)sub_unk3_TownMinishHoles_Cafe, (void*)sub_StateChange_TownMinishHoles_Cafe } },
    { 0x2D, 0x05, { NULL, NULL, (void*)sub_unk3_TownMinishHoles_5, (void*)sub_StateChange_TownMinishHoles_5 } },
    { 0x2D,
      0x10,
      { NULL, NULL, (void*)sub_unk3_TownMinishHoles_LibraryBookshelf,
        (void*)sub_StateChange_TownMinishHoles_LibraryBookshelf } },
    { 0x2D,
      0x11,
      { NULL, NULL, (void*)sub_unk3_TownMinishHoles_LibrariBookHouse,
        (void*)sub_StateChange_TownMinishHoles_LibrariBookHouse } },
    { 0x2D,
      0x12,
      { NULL, NULL, (void*)sub_unk3_TownMinishHoles_RemShoeShop, (void*)sub_StateChange_TownMinishHoles_RemShoeShop } },
    { 0x2E, 0x00, { NULL, NULL, (void*)sub_unk3_MinishRafters_Cafe, (void*)sub_StateChange_MinishRafters_Cafe } },
    { 0x2E,
      0x01,
      { NULL, NULL, (void*)sub_unk3_MinishRafters_Stockwell, (void*)sub_StateChange_MinishRafters_Stockwell } },
    { 0x2E,
      0x02,
      { (void*)sub_unk1_MinishRafters_DrLeft, NULL, (void*)sub_unk3_MinishRafters_DrLeft,
        (void*)sub_StateChange_MinishRafters_DrLeft } },
    { 0x2E, 0x03, { NULL, NULL, (void*)sub_unk3_MinishRafters_Bakery, (void*)sub_StateChange_MinishRafters_Bakery } },
    { 0x2F, 0x00, { NULL, NULL, (void*)sub_unk3_GoronCave_Stairs, (void*)sub_StateChange_GoronCave_Stairs } },
    { 0x2F, 0x01, { NULL, NULL, (void*)sub_unk3_GoronCave_Main, (void*)sub_StateChange_GoronCave_Main } },
    { 0x30,
      0x00,
      { NULL, NULL, (void*)sub_unk3_WindTribeTower_Entrance, (void*)sub_StateChange_WindTribeTower_Entrance } },
    { 0x30, 0x01, { NULL, NULL, (void*)sub_unk3_WindTribeTower_Floor2, (void*)sub_StateChange_WindTribeTower_Floor2 } },
    { 0x30, 0x02, { NULL, NULL, (void*)sub_unk3_WindTribeTower_Floor3, (void*)sub_StateChange_WindTribeTower_Floor3 } },
    { 0x30, 0x03, { NULL, NULL, (void*)sub_unk3_WindTribeTower_Floor4, (void*)sub_StateChange_WindTribeTower_Floor4 } },
    { 0x31,
      0x00,
      { NULL, NULL, (void*)sub_unk3_WindTribeTowerRoof_Main, (void*)sub_StateChange_WindTribeTowerRoof_Main } },
    { 0x32, 0x00, { NULL, NULL, (void*)sub_unk3_Caves_Boomerang, (void*)sub_StateChange_Caves_Boomerang } },
    { 0x32, 0x01, { NULL, NULL, (void*)sub_unk3_Caves_ToGraveyard, (void*)sub_StateChange_Caves_ToGraveyard } },
    { 0x32, 0x02, { NULL, NULL, (void*)sub_unk3_Caves_2, (void*)sub_StateChange_Caves_2 } },
    { 0x32, 0x03, { NULL, NULL, (void*)sub_unk3_Caves_3, (void*)sub_StateChange_Caves_3 } },
    { 0x32, 0x04, { NULL, NULL, (void*)sub_unk3_Caves_4, (void*)sub_StateChange_Caves_4 } },
    { 0x32, 0x05, { NULL, NULL, (void*)sub_unk3_Caves_5, (void*)sub_StateChange_Caves_5 } },
    { 0x32, 0x06, { NULL, NULL, (void*)sub_unk3_Caves_6, (void*)sub_StateChange_Caves_6 } },
    { 0x32,
      0x07,
      { NULL, NULL, (void*)sub_unk3_Caves_TrilbyKeeseChest, (void*)sub_StateChange_Caves_TrilbyKeeseChest } },
    { 0x32,
      0x08,
      { NULL, NULL, (void*)sub_unk3_Caves_TrilbyFairyFountain, (void*)sub_StateChange_Caves_TrilbyFairyFountain } },
    { 0x32,
      0x09,
      { NULL, NULL, (void*)sub_unk3_Caves_SouthHyruleFieldFairyFountain,
        (void*)sub_StateChange_Caves_SouthHyruleFieldFairyFountain } },
    { 0x32, 0x0A, { NULL, NULL, (void*)sub_unk3_Caves_A, (void*)sub_StateChange_Caves_A } },
    { 0x32,
      0x0B,
      { NULL, NULL, (void*)sub_unk3_Caves_HyruleTownWaterfall, (void*)sub_StateChange_Caves_HyruleTownWaterfall } },
    { 0x32, 0x0C, { NULL, NULL, (void*)sub_unk3_Caves_LonLonRanch, (void*)sub_StateChange_Caves_LonLonRanch } },
    { 0x32,
      0x0D,
      { NULL, NULL, (void*)sub_unk3_Caves_LonLonRanchSecret, (void*)sub_StateChange_Caves_LonLonRanchSecret } },
    { 0x32, 0x0E, { NULL, NULL, (void*)sub_unk3_Caves_TrilbyHighlands, (void*)sub_StateChange_Caves_TrilbyHighlands } },
    { 0x32,
      0x0F,
      { NULL, NULL, (void*)sub_unk3_Caves_LonLonRanchWallet, (void*)sub_StateChange_Caves_LonLonRanchWallet } },
    { 0x32,
      0x10,
      { NULL, NULL, (void*)sub_unk3_Caves_SouthHyruleFieldRupee, (void*)sub_StateChange_Caves_SouthHyruleFieldRupee } },
    { 0x32, 0x11, { NULL, NULL, (void*)sub_unk3_Caves_TrilbyRupee, (void*)sub_StateChange_Caves_TrilbyRupee } },
    { 0x32,
      0x12,
      { NULL, NULL, (void*)sub_unk3_Caves_TrilbyMittsFairyFountain,
        (void*)sub_StateChange_Caves_TrilbyMittsFairyFountain } },
    { 0x32, 0x13, { NULL, NULL, (void*)sub_unk3_Caves_HillsKeeseChest, (void*)sub_StateChange_Caves_HillsKeeseChest } },
    { 0x32,
      0x14,
      { NULL, NULL, (void*)sub_unk3_Caves_BottleBusinessScrub, (void*)sub_StateChange_Caves_BottleBusinessScrub } },
    { 0x32,
      0x15,
      { NULL, NULL, (void*)sub_unk3_Caves_HeartPieceHallway, (void*)sub_StateChange_Caves_HeartPieceHallway } },
    { 0x32,
      0x16,
      { NULL, NULL, (void*)sub_unk3_Caves_NorthHyruleFieldFairyFountain,
        (void*)sub_StateChange_Caves_NorthHyruleFieldFairyFountain } },
    { 0x32,
      0x17,
      { NULL, NULL, (void*)sub_unk3_Caves_KinstoneBusinessScrub, (void*)sub_StateChange_Caves_KinstoneBusinessScrub } },
    { 0x33,
      0x00,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_Hallway2F, (void*)sub_StateChange_VeilFallsCaves_Hallway2F } },
    { 0x33,
      0x01,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_Hallway1F, (void*)sub_StateChange_VeilFallsCaves_Hallway1F } },
    { 0x33,
      0x02,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_SecretRoom, (void*)sub_StateChange_VeilFallsCaves_SecretRoom } },
    { 0x33,
      0x03,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_Entrance, (void*)sub_StateChange_VeilFallsCaves_Entrance } },
    { 0x33, 0x04, { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_Exit, (void*)sub_StateChange_VeilFallsCaves_Exit } },
    { 0x33,
      0x05,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_SecretChest, (void*)sub_StateChange_VeilFallsCaves_SecretChest } },
    { 0x33,
      0x06,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_SecretStaircases,
        (void*)sub_StateChange_VeilFallsCaves_SecretStaircases } },
    { 0x33,
      0x07,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_BlockPuzzle, (void*)sub_StateChange_VeilFallsCaves_BlockPuzzle } },
    { 0x33,
      0x08,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_RupeePath, (void*)sub_StateChange_VeilFallsCaves_RupeePath } },
    { 0x33,
      0x09,
      { NULL, NULL, (void*)sub_unk3_VeilFallsCaves_HeartPiece, (void*)sub_StateChange_VeilFallsCaves_HeartPiece } },
    { 0x34,
      0x00,
      { NULL, NULL, (void*)sub_unk3_RoyalValleyGraves_HeartPiece,
        (void*)sub_StateChange_RoyalValleyGraves_HeartPiece } },
    { 0x34,
      0x01,
      { NULL, NULL, (void*)sub_unk3_RoyalValleyGraves_Gina, (void*)sub_StateChange_RoyalValleyGraves_Gina } },
    { 0x35, 0x00, { NULL, NULL, (void*)sub_unk3_MinishCaves_BeanPesto, (void*)sub_StateChange_MinishCaves_BeanPesto } },
    { 0x35,
      0x01,
      { NULL, NULL, (void*)sub_unk3_MinishCaves_SoutheastWater1, (void*)sub_StateChange_MinishCaves_SoutheastWater1 } },
    { 0x35,
      0x02,
      { NULL, NULL, (void*)sub_unk3_MinishCaves_SoutheastWater2, (void*)sub_StateChange_MinishCaves_SoutheastWater2 } },
    { 0x35, 0x03, { NULL, NULL, (void*)sub_unk3_MinishCaves_Ruins, (void*)sub_StateChange_MinishCaves_Ruins } },
    { 0x35,
      0x04,
      { NULL, NULL, (void*)sub_unk3_MinishCaves_OutsideLinksHouse,
        (void*)sub_StateChange_MinishCaves_OutsideLinksHouse } },
    { 0x35,
      0x05,
      { NULL, NULL, (void*)sub_unk3_MinishCaves_MinishWoodsNorth1,
        (void*)sub_StateChange_MinishCaves_MinishWoodsNorth1 } },
    { 0x35,
      0x06,
      { NULL, NULL, (void*)sub_unk3_MinishCaves_MinishWoodsNorth2,
        (void*)sub_StateChange_MinishCaves_MinishWoodsNorth2 } },
    { 0x35,
      0x07,
      { NULL, NULL, (void*)sub_unk3_MinishCaves_LakeHyliaNorth, (void*)sub_StateChange_MinishCaves_LakeHyliaNorth } },
    { 0x35,
      0x08,
      { NULL, NULL, (void*)sub_unk3_MinishCaves_LakeHyliaLibrari,
        (void*)sub_StateChange_MinishCaves_LakeHyliaLibrari } },
    { 0x35,
      0x09,
      { NULL, NULL, (void*)sub_unk3_MinishCaves_MinishWoodsSouthwest,
        (void*)sub_StateChange_MinishCaves_MinishWoodsSouthwest } },
    { 0x36,
      0x00,
      { NULL, NULL, (void*)sub_unk3_CastleGardenMinishHoles_East,
        (void*)sub_StateChange_CastleGardenMinishHoles_East } },
    { 0x36,
      0x01,
      { NULL, NULL, (void*)sub_unk3_CastleGardenMinishHoles_West,
        (void*)sub_StateChange_CastleGardenMinishHoles_West } },
    { 0x37, 0x00, { NULL, NULL, (void*)sub_unk3_37_0, (void*)sub_StateChange_37_0 } },
    { 0x37, 0x01, { NULL, NULL, (void*)sub_unk3_37_1, (void*)sub_StateChange_37_1 } },
    { 0x38, 0x00, { NULL, NULL, (void*)sub_unk3_EzloAuxCutscene_Main, (void*)sub_StateChange_EzloAuxCutscene_Main } },
    { 0x41,
      0x00,
      { NULL, NULL, (void*)sub_unk3_HyruleTownUnderground_Main, (void*)sub_StateChange_HyruleTownUnderground_Main } },
    { 0x41,
      0x01,
      { NULL, NULL, (void*)sub_unk3_HyruleTownUnderground_Well, (void*)sub_StateChange_HyruleTownUnderground_Well } },
    { 0x42, 0x00, { NULL, NULL, (void*)sub_unk3_GardenFountains_East, (void*)sub_StateChange_GardenFountains_East } },
    { 0x42, 0x01, { NULL, NULL, (void*)sub_unk3_GardenFountains_West, (void*)sub_StateChange_GardenFountains_West } },
    { 0x43, 0x00, { NULL, NULL, (void*)sub_unk3_GreatFairies_Entrance, (void*)sub_StateChange_GreatFairies_Entrance } },
    { 0x43, 0x01, { NULL, NULL, (void*)sub_unk3_GreatFairies_Exit, (void*)sub_StateChange_GreatFairies_Exit } },
    { 0x44, 0x00, { NULL, NULL, (void*)sub_unk3_SimonsSimulation_Main, (void*)sub_StateChange_SimonsSimulation_Main } },
    { 0x45, 0x00, { NULL, NULL, (void*)sub_unk3_45_Main, (void*)sub_StateChange_45_Main } },
    { 0x47, 0x00, { NULL, NULL, (void*)sub_unk3_47_0, (void*)sub_StateChange_47_0 } },
    { 0x47, 0x01, { NULL, NULL, (void*)sub_unk3_47_1, (void*)sub_StateChange_47_1 } },
    { 0x47, 0x02, { NULL, NULL, (void*)sub_unk3_47_2, (void*)sub_StateChange_47_2 } },
    { 0x47, 0x03, { NULL, NULL, (void*)sub_unk3_47_3, (void*)sub_StateChange_47_3 } },
    { 0x47, 0x04, { NULL, NULL, (void*)sub_unk3_47_4, (void*)sub_StateChange_47_4 } },
    { 0x48,
      0x00,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Madderpillar, (void*)sub_StateChange_DeepwoodShrine_Madderpillar } },
    { 0x48,
      0x01,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_BluePortal, (void*)sub_StateChange_DeepwoodShrine_BluePortal } },
    { 0x48,
      0x02,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_StairsToB1, (void*)sub_StateChange_DeepwoodShrine_StairsToB1 } },
    { 0x48,
      0x03,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_PotBridge, (void*)sub_StateChange_DeepwoodShrine_PotBridge } },
    { 0x48,
      0x04,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_DoubleStatue, (void*)sub_StateChange_DeepwoodShrine_DoubleStatue } },
    { 0x48, 0x05, { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Map, (void*)sub_StateChange_DeepwoodShrine_Map } },
    { 0x48, 0x06, { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Barrel, (void*)sub_StateChange_DeepwoodShrine_Barrel } },
    { 0x48, 0x07, { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Button, (void*)sub_StateChange_DeepwoodShrine_Button } },
    { 0x48,
      0x08,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Mulldozer, (void*)sub_StateChange_DeepwoodShrine_Mulldozer } },
    { 0x48,
      0x09,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Pillars, (void*)sub_StateChange_DeepwoodShrine_Pillars } },
    { 0x48, 0x0A, { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Lever, (void*)sub_StateChange_DeepwoodShrine_Lever } },
    { 0x48,
      0x0B,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Entrance, (void*)sub_StateChange_DeepwoodShrine_Entrance } },
    { 0x48, 0x10, { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Torch, (void*)sub_StateChange_DeepwoodShrine_Torch } },
    { 0x48,
      0x11,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_BossKey, (void*)sub_StateChange_DeepwoodShrine_BossKey } },
    { 0x48,
      0x12,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_Compass, (void*)sub_StateChange_DeepwoodShrine_Compass } },
    { 0x48, 0x13, { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_13, (void*)sub_StateChange_DeepwoodShrine_13 } },
    { 0x48,
      0x14,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_LilyPadWest, (void*)sub_StateChange_DeepwoodShrine_LilyPadWest } },
    { 0x48,
      0x15,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_LilyPadEast, (void*)sub_StateChange_DeepwoodShrine_LilyPadEast } },
    { 0x48,
      0x17,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_PreBoss, (void*)sub_StateChange_DeepwoodShrine_PreBoss } },
    { 0x48,
      0x20,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrine_InsideBarrel, (void*)sub_StateChange_DeepwoodShrine_InsideBarrel } },
    { 0x49,
      0x00,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrineBoss_Main, (void*)sub_StateChange_DeepwoodShrineBoss_Main } },
    { 0x4A,
      0x00,
      { NULL, NULL, (void*)sub_unk3_DeepwoodShrineEntry_Main, (void*)sub_StateChange_DeepwoodShrineEntry_Main } },
    { 0x4D, 0x00, { NULL, NULL, (void*)sub_unk3_4D_Main, (void*)sub_StateChange_4D_Main } },
    { 0x50,
      0x00,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_AfterCane, (void*)sub_StateChange_CaveOfFlames_AfterCane } },
    { 0x50,
      0x01,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_SpinyChus, (void*)sub_StateChange_CaveOfFlames_SpinyChus } },
    { 0x50,
      0x02,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_CartToSpinyChus,
        (void*)sub_StateChange_CaveOfFlames_CartToSpinyChus } },
    { 0x50, 0x03, { NULL, NULL, (void*)sub_unk3_CaveOfFlames_Entrance, (void*)sub_StateChange_CaveOfFlames_Entrance } },
    { 0x50, 0x04, { NULL, NULL, (void*)sub_unk3_CaveOfFlames_MainCart, (void*)sub_StateChange_CaveOfFlames_MainCart } },
    { 0x50,
      0x05,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_NorthEntrance, (void*)sub_StateChange_CaveOfFlames_NorthEntrance } },
    { 0x50, 0x06, { NULL, NULL, (void*)sub_unk3_CaveOfFlames_CartWest, (void*)sub_StateChange_CaveOfFlames_CartWest } },
    { 0x50,
      0x07,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_Helmasaur, (void*)sub_StateChange_CaveOfFlames_Helmasaur } },
    { 0x50,
      0x08,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_Rollobite, (void*)sub_StateChange_CaveOfFlames_Rollobite } },
    { 0x50,
      0x09,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_MinishLava, (void*)sub_StateChange_CaveOfFlames_MinishLava } },
    { 0x50,
      0x10,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_MinishSpikes, (void*)sub_StateChange_CaveOfFlames_MinishSpikes } },
    { 0x50,
      0x11,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_RollobiteSwitch,
        (void*)sub_StateChange_CaveOfFlames_RollobiteSwitch } },
    { 0x50,
      0x12,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_BeforeGleerok, (void*)sub_StateChange_CaveOfFlames_BeforeGleerok } },
    { 0x50,
      0x13,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_PathBossKey, (void*)sub_StateChange_CaveOfFlames_PathBossKey } },
    { 0x50,
      0x14,
      { NULL, NULL, (void*)sub_unk3_CaveOfFlames_PathBossKey2, (void*)sub_StateChange_CaveOfFlames_PathBossKey2 } },
    { 0x50, 0x15, { NULL, NULL, (void*)sub_unk3_CaveOfFlames_Compass, (void*)sub_StateChange_CaveOfFlames_Compass } },
    { 0x50, 0x16, { NULL, NULL, (void*)sub_unk3_CaveOfFlames_Bobomb, (void*)sub_StateChange_CaveOfFlames_Bobomb } },
    { 0x50, 0x17, { NULL, NULL, (void*)sub_unk3_CaveOfFlames_BossDoor, (void*)sub_StateChange_CaveOfFlames_BossDoor } },
    { 0x50, 0x18, { NULL, NULL, (void*)sub_unk3_CaveOfFlames_18, (void*)sub_StateChange_CaveOfFlames_18 } },
    { 0x50, 0x20, { NULL, NULL, (void*)sub_unk3_CaveOfFlamesBoss_Main, (void*)sub_StateChange_CaveOfFlamesBoss_Main } },
    { 0x51, 0x00, { NULL, NULL, (void*)sub_unk3_CaveOfFlamesBoss_Main, (void*)sub_StateChange_CaveOfFlamesBoss_Main } },
    { 0x57, 0x00, { NULL, NULL, (void*)sub_unk3_57_Main, (void*)sub_StateChange_57_Main } },
    { 0x58,
      0x00,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Eyegore, (void*)sub_StateChange_FortressOfWinds_Eyegore } },
    { 0x58,
      0x01,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_BeforeMazaal,
        (void*)sub_StateChange_FortressOfWinds_BeforeMazaal } },
    { 0x58,
      0x02,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_EastKeyLever,
        (void*)sub_StateChange_FortressOfWinds_EastKeyLever } },
    { 0x58,
      0x03,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_PitPlatforms,
        (void*)sub_StateChange_FortressOfWinds_PitPlatforms } },
    { 0x58,
      0x04,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_WestKeyLever,
        (void*)sub_StateChange_FortressOfWinds_WestKeyLever } },
    { 0x58,
      0x05,
      { NULL, NULL, (void*)sub_unk3_OuterFortressOfWinds_MoleMitts,
        (void*)sub_StateChange_OuterFortressOfWinds_MoleMitts } },
    { 0x58,
      0x06,
      { NULL, NULL, (void*)sub_unk3_OuterFortressOfWinds_SmallKey,
        (void*)sub_StateChange_OuterFortressOfWinds_SmallKey } },
    { 0x58,
      0x10,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Darknut, (void*)sub_StateChange_FortressOfWinds_Darknut } },
    { 0x58,
      0x11,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_EyeBridge, (void*)sub_StateChange_FortressOfWinds_EyeBridge } },
    { 0x58, 0x12, { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Pit, (void*)sub_StateChange_FortressOfWinds_Pit } },
    { 0x58,
      0x13,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Wallmaster, (void*)sub_StateChange_FortressOfWinds_Wallmaster } },
    { 0x58, 0x14, { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Clone, (void*)sub_StateChange_FortressOfWinds_Clone } },
    { 0x58,
      0x15,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_SpikeTraps, (void*)sub_StateChange_FortressOfWinds_SpikeTraps } },
    { 0x58,
      0x16,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Mazaal, (void*)sub_StateChange_FortressOfWinds_Mazaal } },
    { 0x58,
      0x17,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Stalfos, (void*)sub_StateChange_FortressOfWinds_Stalfos } },
    { 0x58,
      0x18,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_EntranceMoleMitts, (void*)sub_StateChange_Dojos_Waveblade0 } },
    { 0x58, 0x19, { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Main2F, (void*)sub_StateChange_Dojos_Waveblade1 } },
    { 0x58, 0x1A, { NULL, NULL, (void*)sub_unk3_FortressOfWinds_MinishHole, (void*)sub_StateChange_Dojos_Waveblade2 } },
    { 0x58, 0x1B, { NULL, NULL, (void*)sub_unk3_FortressOfWinds_BossKey, (void*)sub_StateChange_Dojos_Waveblade3 } },
    { 0x58,
      0x1C,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_WestStairs2F, (void*)sub_StateChange_Dojos_Waveblade4 } },
    { 0x58,
      0x1D,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_EastStairs2F, (void*)sub_StateChange_Dojos_Waveblade5 } },
    { 0x58,
      0x20,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_WestStairs1F, (void*)sub_StateChange_Dojos_Waveblade6 } },
    { 0x58,
      0x21,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_CenterStairs1F, (void*)sub_StateChange_Dojos_Waveblade7 } },
    { 0x58,
      0x22,
      { NULL, NULL, (void*)sub_unk3_FortressOfWinds_EastStairs1F, (void*)sub_StateChange_Dojos_Waveblade8 } },
    { 0x58, 0x23, { NULL, NULL, (void*)sub_unk3_FortressOfWinds_Wizzrobe, (void*)sub_StateChange_Dojos_Waveblade9 } },
    { 0x58, 0x24, { NULL, NULL, (void*)sub_unk3_FortressOfWinds_HeartPiece, (void*)sub_StateChange_Dojos_70 } },
    { 0x59,
      0x00,
      { NULL, NULL, (void*)sub_unk3_FortressOfWindsTop_Main, (void*)sub_StateChange_FortressOfWindsTop_Main } },
    { 0x5A, 0x00, { NULL, NULL, (void*)sub_unk3_InnerMazaal_Main, (void*)sub_StateChange_Dojos_71 } },
    { 0x5A, 0x01, { NULL, NULL, (void*)sub_unk3_InnerMazaal_Main, (void*)sub_StateChange_Dojos_71 } },
    { 0x5F, 0x00, { NULL, NULL, (void*)sub_unk3_5F_Main, (void*)sub_StateChange_5F_Main } },
    { 0x60,
      0x00,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_WestHole, (void*)sub_StateChange_TempleOfDroplets_WestHole } },
    { 0x60,
      0x01,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_NorthSplit, (void*)sub_StateChange_TempleOfDroplets_NorthSplit } },
    { 0x60,
      0x02,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_EastHole, (void*)sub_StateChange_TempleOfDroplets_EastHole } },
    { 0x60,
      0x03,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_Entrance, (void*)sub_StateChange_TempleOfDroplets_Entrance } },
    { 0x60,
      0x04,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_NorthwestStairs,
        (void*)sub_StateChange_TempleOfDroplets_NorthwestStairs } },
    { 0x60,
      0x05,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_ScissorsMiniboss,
        (void*)sub_StateChange_TempleOfDroplets_ScissorsMiniboss } },
    { 0x60,
      0x06,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_WaterfallNorthwest,
        (void*)sub_StateChange_TempleOfDroplets_WaterfallNorthwest } },
    { 0x60,
      0x07,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_WaterfallNortheast,
        (void*)sub_StateChange_TempleOfDroplets_WaterfallNortheast } },
    { 0x60,
      0x08,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_Element, (void*)sub_StateChange_TempleOfDroplets_Element } },
    { 0x60,
      0x09,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_IceCorner, (void*)sub_StateChange_TempleOfDroplets_IceCorner } },
    { 0x60,
      0x0A,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_IcePitMaze, (void*)sub_StateChange_TempleOfDroplets_IcePitMaze } },
    { 0x60,
      0x0B,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_HoleToBlueChuchu,
        (void*)sub_StateChange_TempleOfDroplets_HoleToBlueChuchu } },
    { 0x60,
      0x0C,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_WaterfallSoutheast,
        (void*)sub_StateChange_TempleOfDroplets_WaterfallSoutheast } },
    { 0x60,
      0x0D,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_WaterfallSouthwest,
        (void*)sub_StateChange_TempleOfDroplets_WaterfallSouthwest } },
    { 0x60,
      0x0E,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BigOcto, (void*)sub_StateChange_TempleOfDroplets_BigOcto } },
    { 0x60, 0x0F, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_ToBigBlueChuchu, (void*)sub_StateChange_Dojos_72 } },
    { 0x60,
      0x10,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BigBlueChuchu,
        (void*)sub_StateChange_TempleOfDroplets_BigBlueChuchu } },
    { 0x60,
      0x11,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BigBlueChuchuKey,
        (void*)sub_StateChange_TempleOfDroplets_BigBlueChuchuKey } },
    { 0x60, 0x20, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BossKey, (void*)sub_StateChange_Dojos_73 } },
    { 0x60, 0x21, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_NorthSmallKey, (void*)sub_StateChange_Dojos_74 } },
    { 0x60,
      0x22,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BlockCloneButtonPuzzle, (void*)sub_StateChange_Dojos_75 } },
    { 0x60, 0x23, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BlockClonePuzzle, (void*)sub_StateChange_Dojos_76 } },
    { 0x60,
      0x24,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BlockCloneIceBridge, (void*)sub_StateChange_Dojos_77 } },
    { 0x60,
      0x25,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_StairsToScissorsMiniboss, (void*)sub_StateChange_Dojos_78 } },
    { 0x60, 0x26, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_SpikeBar, (void*)sub_StateChange_Dojos_79 } },
    { 0x60, 0x27, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_Lanterns, (void*)sub_StateChange_Dojos_80 } },
    { 0x60,
      0x28,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_LilypadIceBlocks,
        (void*)sub_StateChange_TempleOfDroplets_LilypadIceBlocks } },
    { 0x60, 0x29, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_Pit, (void*)sub_StateChange_Dojos_81 } },
    { 0x60, 0x2A, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_FireBars, (void*)sub_StateChange_Dojos_82 } },
    { 0x60, 0x2B, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_LanternMaze, (void*)sub_StateChange_Dojos_83 } },
    { 0x60, 0x2C, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_Madderpillars, (void*)sub_StateChange_Dojos_84 } },
    { 0x60,
      0x2D,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_AfterMadderpillars, (void*)sub_StateChange_Dojos_85 } },
    { 0x60,
      0x2E,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BlueChuchuKeyLever,
        (void*)sub_StateChange_TempleOfDroplets_BlueChuchuKeyLever } },
    { 0x60, 0x2F, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_MulldozerKey, (void*)sub_StateChange_Dojos_86 } },
    { 0x60, 0x30, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_BombWall, (void*)sub_StateChange_Dojos_87 } },
    { 0x60, 0x31, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_LilypadWestB2, (void*)sub_StateChange_Dojos_88 } },
    { 0x60, 0x32, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_CompassRoom, (void*)sub_StateChange_Dojos_89 } },
    { 0x60, 0x33, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_LanternScissors, (void*)sub_StateChange_Dojos_90 } },
    { 0x60, 0x34, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_LilypadMiddleB2, (void*)sub_StateChange_Dojos_91 } },
    { 0x60, 0x35, { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_LilypadEastB2, (void*)sub_StateChange_Dojos_92 } },
    { 0x60,
      0x36,
      { NULL, NULL, (void*)sub_unk3_TempleOfDroplets_FlameBarBlockPuzzle, (void*)sub_StateChange_Dojos_93 } },
    { 0x62,
      0x00,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_Entrance,
        (void*)sub_StateChange_HyruleTownMinishCaves_Entrance } },
    { 0x62,
      0x01,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_NorthRoom,
        (void*)sub_StateChange_HyruleTownMinishCaves_NorthRoom } },
    { 0x62,
      0x02,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_PacciJump,
        (void*)sub_StateChange_HyruleTownMinishCaves_PacciJump } },
    { 0x62,
      0x03,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_MulldozerFight,
        (void*)sub_StateChange_HyruleTownMinishCaves_MulldozerFight } },
    { 0x62,
      0x04,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_WestChest,
        (void*)sub_StateChange_HyruleTownMinishCaves_WestChest } },
    { 0x62,
      0x10,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_Flippers,
        (void*)sub_StateChange_HyruleTownMinishCaves_Flippers } },
    { 0x62,
      0x11,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_Librari,
        (void*)sub_StateChange_HyruleTownMinishCaves_Librari } },
    { 0x62,
      0x12,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_WestFrozenChest,
        (void*)sub_StateChange_HyruleTownMinishCaves_WestFrozenChest } },
    { 0x62,
      0x13,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_CrossIntersection,
        (void*)sub_StateChange_HyruleTownMinishCaves_CrossIntersection } },
    { 0x62,
      0x14,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_SoutheastCorner,
        (void*)sub_StateChange_HyruleTownMinishCaves_SoutheastCorner } },
    { 0x62,
      0x15,
      { NULL, NULL, (void*)sub_unk3_HyruleTownMinishCaves_Entrance2,
        (void*)sub_StateChange_HyruleTownMinishCaves_Entrance2 } },
    { 0x67, 0x00, { NULL, NULL, (void*)sub_unk3_67_Main, (void*)sub_StateChange_67_Main } },
    { 0x68, 0x00, { NULL, NULL, (void*)sub_unk3_RoyalCrypt_KingGustaf, (void*)sub_StateChange_RoyalCrypt_KingGustaf } },
    { 0x68, 0x01, { NULL, NULL, (void*)sub_unk3_RoyalCrypt_WaterRope, (void*)sub_StateChange_Dojos_94 } },
    { 0x68, 0x02, { NULL, NULL, (void*)sub_unk3_RoyalCrypt_Gibdo, (void*)sub_StateChange_Dojos_95 } },
    { 0x68, 0x03, { NULL, NULL, (void*)sub_unk3_RoyalCrypt_3, (void*)sub_StateChange_Dojos_96 } },
    { 0x68, 0x04, { NULL, NULL, (void*)sub_unk3_RoyalCrypt_KeyBlock, (void*)sub_StateChange_Dojos_97 } },
    { 0x68, 0x05, { NULL, NULL, (void*)sub_unk3_RoyalCrypt_5, (void*)sub_StateChange_Dojos_98 } },
    { 0x68, 0x06, { NULL, NULL, (void*)sub_unk3_RoyalCrypt_6, (void*)sub_StateChange_Dojos_99 } },
    { 0x68,
      0x07,
      { NULL, NULL, (void*)sub_unk3_RoyalCrypt_MushroomPit, (void*)sub_StateChange_RoyalCrypt_MushroomPit } },
    { 0x68, 0x08, { NULL, NULL, (void*)sub_unk3_RoyalCrypt_Entrance, (void*)sub_StateChange_RoyalCrypt_Entrance } },
    { 0x6F, 0x00, { NULL, NULL, (void*)sub_unk3_6F_Main, (void*)sub_StateChange_6F_Main } },
    { 0x70,
      0x00,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_GyorgTornado, (void*)sub_StateChange_PalaceOfWinds_GyorgTornado } },
    { 0x70, 0x01, { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BossKey, (void*)sub_StateChange_PalaceOfWinds_BossKey } },
    { 0x70,
      0x02,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BeforeBallAndChainSoldiers,
        (void*)sub_StateChange_PalaceOfWinds_BeforeBallAndChainSoldiers } },
    { 0x70,
      0x03,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_GyorgBossDoor, (void*)sub_StateChange_PalaceOfWinds_GyorgBossDoor } },
    { 0x70,
      0x04,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_EastChestFromGyorgBossDoor,
        (void*)sub_StateChange_PalaceOfWinds_EastChestFromGyorgBossDoor } },
    { 0x70,
      0x05,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_MoblinAndWizzrobeFight,
        (void*)sub_StateChange_PalaceOfWinds_MoblinAndWizzrobeFight } },
    { 0x70,
      0x06,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_FourButtonStalfos,
        (void*)sub_StateChange_PalaceOfWinds_FourButtonStalfos } },
    { 0x70,
      0x07,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_FanAndKeyToBossKey,
        (void*)sub_StateChange_PalaceOfWinds_FanAndKeyToBossKey } },
    { 0x70,
      0x08,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BallAndChainSoldiers,
        (void*)sub_StateChange_PalaceOfWinds_BallAndChainSoldiers } },
    { 0x70,
      0x09,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BombarossaPath,
        (void*)sub_StateChange_PalaceOfWinds_BombarossaPath } },
    { 0x70,
      0x0A,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_HoleToDarknut, (void*)sub_StateChange_PalaceOfWinds_HoleToDarknut } },
    { 0x70,
      0x0B,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_ToBombarossaPath,
        (void*)sub_StateChange_PalaceOfWinds_ToBombarossaPath } },
    { 0x70,
      0x0C,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_DarknutMiniboss,
        (void*)sub_StateChange_PalaceOfWinds_DarknutMiniboss } },
    { 0x70,
      0x0D,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BombWallInside,
        (void*)sub_StateChange_PalaceOfWinds_BombWallInside } },
    { 0x70,
      0x0E,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BombWallOutside,
        (void*)sub_StateChange_PalaceOfWinds_BombWallOutside } },
    { 0x70,
      0x0F,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_CloudJumps, (void*)sub_StateChange_PalaceOfWinds_CloudJumps } },
    { 0x70,
      0x10,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BlockMazeToBossDoor,
        (void*)sub_StateChange_PalaceOfWinds_BlockMazeToBossDoor } },
    { 0x70,
      0x11,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_CrackedFloorLakitu,
        (void*)sub_StateChange_PalaceOfWinds_CrackedFloorLakitu } },
    { 0x70,
      0x12,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_HeartPieceBridge,
        (void*)sub_StateChange_PalaceOfWinds_HeartPieceBridge } },
    { 0x70,
      0x13,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_FanBridge, (void*)sub_StateChange_PalaceOfWinds_FanBridge } },
    { 0x70,
      0x14,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_ToFanBridge, (void*)sub_StateChange_PalaceOfWinds_ToFanBridge } },
    { 0x70,
      0x15,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_RedWarpHall, (void*)sub_StateChange_PalaceOfWinds_RedWarpHall } },
    { 0x70,
      0x16,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_PlatformCloneRide,
        (void*)sub_StateChange_PalaceOfWinds_PlatformCloneRide } },
    { 0x70,
      0x17,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_PitCornerAfterKey,
        (void*)sub_StateChange_PalaceOfWinds_PitCornerAfterKey } },
    { 0x70,
      0x18,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_CrowRide, (void*)sub_StateChange_PalaceOfWinds_CrowRide } },
    { 0x70,
      0x19,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_GratePlatformRide,
        (void*)sub_StateChange_PalaceOfWinds_GratePlatformRide } },
    { 0x70, 0x1A, { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_PotPush, (void*)sub_StateChange_PalaceOfWinds_PotPush } },
    { 0x70,
      0x1B,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_FloormasterLever,
        (void*)sub_StateChange_PalaceOfWinds_FloormasterLever } },
    { 0x70, 0x1C, { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_Map, (void*)sub_StateChange_PalaceOfWinds_Map } },
    { 0x70,
      0x1D,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_CornerToMap, (void*)sub_StateChange_PalaceOfWinds_CornerToMap } },
    { 0x70,
      0x1E,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_StairsAfterFloormaster,
        (void*)sub_StateChange_PalaceOfWinds_StairsAfterFloormaster } },
    { 0x70,
      0x1F,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_HoleToKinstoneWizzrobe,
        (void*)sub_StateChange_PalaceOfWinds_HoleToKinstoneWizzrobe } },
    { 0x70,
      0x20,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_KeyArrowButton,
        (void*)sub_StateChange_PalaceOfWinds_KeyArrowButton } },
    { 0x70,
      0x21,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_GratesTo3F, (void*)sub_StateChange_PalaceOfWinds_GratesTo3F } },
    { 0x70,
      0x22,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_SpinyFight, (void*)sub_StateChange_PalaceOfWinds_SpinyFight } },
    { 0x70,
      0x23,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_PeahatSwitch, (void*)sub_StateChange_PalaceOfWinds_PeahatSwitch } },
    { 0x70,
      0x24,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_WhirlwindBombarossa,
        (void*)sub_StateChange_PalaceOfWinds_WhirlwindBombarossa } },
    { 0x70,
      0x25,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_DoorToStalfosFirebar,
        (void*)sub_StateChange_PalaceOfWinds_DoorToStalfosFirebar } },
    { 0x70,
      0x26,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_StalfosFireborHole,
        (void*)sub_StateChange_PalaceOfWinds_StalfosFireborHole } },
    { 0x70,
      0x27,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_ShortcutDoorButtons,
        (void*)sub_StateChange_PalaceOfWinds_ShortcutDoorButtons } },
    { 0x70,
      0x28,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_ToPeahatSwitch,
        (void*)sub_StateChange_PalaceOfWinds_ToPeahatSwitch } },
    { 0x70,
      0x29,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_KinstoneWizzrobeFight,
        (void*)sub_StateChange_PalaceOfWinds_KinstoneWizzrobeFight } },
    { 0x70,
      0x2A,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_GibdoStairs, (void*)sub_StateChange_PalaceOfWinds_GibdoStairs } },
    { 0x70,
      0x2B,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_SpikeBarSmallKey,
        (void*)sub_StateChange_PalaceOfWinds_SpikeBarSmallKey } },
    { 0x70, 0x2C, { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_RocCape, (void*)sub_StateChange_PalaceOfWinds_RocCape } },
    { 0x70,
      0x2D,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_FireBarGrates, (void*)sub_StateChange_PalaceOfWinds_FireBarGrates } },
    { 0x70,
      0x2E,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_PlatformRideBombarossas,
        (void*)sub_StateChange_PalaceOfWinds_PlatformRideBombarossas } },
    { 0x70,
      0x2F,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BridgeAfterDarknut,
        (void*)sub_StateChange_PalaceOfWinds_BridgeAfterDarknut } },
    { 0x70,
      0x30,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_BridgeSwitchesCloneBlock,
        (void*)sub_StateChange_PalaceOfWinds_BridgeSwitchesCloneBlock } },
    { 0x70,
      0x31,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_EntranceRoom, (void*)sub_StateChange_PalaceOfWinds_EntranceRoom } },
    { 0x70,
      0x32,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWinds_DarkCompassHall,
        (void*)sub_StateChange_PalaceOfWinds_DarkCompassHall } },
    { 0x71,
      0x00,
      { NULL, NULL, (void*)sub_unk3_PalaceOfWindsBoss_Main, (void*)sub_StateChange_PalaceOfWindsBoss_Main } },
    { 0x77, 0x00, { NULL, NULL, (void*)sub_unk3_77_Main, (void*)sub_StateChange_77_Main } },
    { 0x78, 0x00, { NULL, NULL, (void*)sub_unk3_Sanctuary_Hall, (void*)sub_StateChange_Sanctuary_Hall } },
    { 0x78, 0x01, { NULL, NULL, (void*)sub_unk3_Sanctuary_Main, (void*)sub_StateChange_Sanctuary_Main } },
    { 0x78,
      0x02,
      { NULL, NULL, (void*)sub_unk3_Sanctuary_StainedGlass, (void*)sub_StateChange_Sanctuary_StainedGlass } },
    { 0x78,
      0x04,
      { NULL, NULL, (void*)sub_unk3_Sanctuary_StainedGlass, (void*)sub_StateChange_Sanctuary_StainedGlass } },
    { 0x7F, 0x00, { NULL, NULL, (void*)sub_unk3_7F_Main, (void*)sub_StateChange_7F_Main } },
    { 0x80, 0x00, { NULL, NULL, (void*)sub_unk3_HyruleCastle_0, (void*)sub_StateChange_HyruleCastle_0 } },
    { 0x80, 0x01, { NULL, NULL, (void*)sub_unk3_HyruleCastle_1, (void*)sub_StateChange_HyruleCastle_1 } },
    { 0x80, 0x02, { NULL, NULL, (void*)sub_unk3_HyruleCastle_2, (void*)sub_StateChange_HyruleCastle_2 } },
    { 0x80, 0x03, { NULL, NULL, (void*)sub_unk3_HyruleCastle_3, (void*)sub_StateChange_HyruleCastle_3 } },
    { 0x80, 0x04, { NULL, NULL, (void*)sub_unk3_HyruleCastle_4, (void*)sub_StateChange_HyruleCastle_4 } },
    { 0x80, 0x05, { NULL, NULL, (void*)sub_unk3_HyruleCastle_5, (void*)sub_StateChange_HyruleCastle_5 } },
    { 0x81,
      0x00,
      { NULL, NULL, (void*)sub_unk3_SanctuaryEntrance_Main, (void*)sub_StateChange_SanctuaryEntrance_Main } },
    { 0x87, 0x00, { NULL, NULL, (void*)sub_unk3_87_Main, (void*)sub_StateChange_87_Main } },
    { 0x88,
      0x00,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FEntrance, (void*)sub_StateChange_DarkHyruleCastle_1FEntrance } },
    { 0x88,
      0x01,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_3FTopLeftTower, (void*)sub_StateChange_Dojos_ToGrimblade7 } },
    { 0x88,
      0x02,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_3FTopRightTower, (void*)sub_StateChange_Dojos_ToGrimblade8 } },
    { 0x88,
      0x03,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_3FBottomLeftTower, (void*)sub_StateChange_Dojos_ToGrimblade9 } },
    { 0x88,
      0x04,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_3FBottomRightTower, (void*)sub_StateChange_Dojos_ToSplitblade0 } },
    { 0x88,
      0x05,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_3FKeatonHallToVaati,
        (void*)sub_StateChange_Dojos_ToSplitblade1 } },
    { 0x88,
      0x06,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_3FTripleDarknut,
        (void*)sub_StateChange_DarkHyruleCastle_3FTripleDarknut } },
    { 0x88,
      0x07,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FTopLeftTower, (void*)sub_StateChange_Dojos_ToSplitblade2 } },
    { 0x88,
      0x08,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FTopLeftCorner, (void*)sub_StateChange_Dojos_ToSplitblade3 } },
    { 0x88,
      0x09,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBossKey, (void*)sub_StateChange_Dojos_ToSplitblade4 } },
    { 0x88,
      0x0A,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBlueWarp, (void*)sub_StateChange_Dojos_ToSplitblade5 } },
    { 0x88,
      0x0B,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FTopRightCornerGhini,
        (void*)sub_StateChange_Dojos_ToSplitblade6 } },
    { 0x88,
      0x0C,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FTopRightCornerTorches,
        (void*)sub_StateChange_Dojos_ToSplitblade7 } },
    { 0x88,
      0x0D,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FTopRightTower, (void*)sub_StateChange_Dojos_ToSplitblade8 } },
    { 0x88,
      0x0E,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FTopLeftDarknut,
        (void*)sub_StateChange_DarkHyruleCastle_2FTopLeftDarknut } },
    { 0x88,
      0x0F,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FSparks, (void*)sub_StateChange_Dojos_ToSplitblade9 } },
    { 0x88,
      0x10,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FTopRightDarknuts,
        (void*)sub_StateChange_DarkHyruleCastle_2FTopRightDarknuts } },
    { 0x88, 0x11, { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FLeft, (void*)sub_StateChange_Dojos_ToGreatblade0 } },
    { 0x88,
      0x12,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FRight, (void*)sub_StateChange_Dojos_ToGreatblade1 } },
    { 0x88,
      0x13,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBottomLeftDarknuts,
        (void*)sub_StateChange_DarkHyruleCastle_2FBottomLeftDarknuts } },
    { 0x88,
      0x14,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBossDoor, (void*)sub_StateChange_Dojos_ToGreatblade2 } },
    { 0x88,
      0x15,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBottomRightDarknut,
        (void*)sub_StateChange_DarkHyruleCastle_2FBottomRightDarknut } },
    { 0x88,
      0x16,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBottomLeftCornerPuzzle,
        (void*)sub_StateChange_Dojos_ToGreatblade3 } },
    { 0x88,
      0x17,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FEntrance, (void*)sub_StateChange_Dojos_ToGreatblade4 } },
    { 0x88,
      0x18,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBottomLeftCorner, (void*)sub_StateChange_Dojos_ToGreatblade5 } },
    { 0x88,
      0x19,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBottomLeftTower, (void*)sub_StateChange_Dojos_ToGreatblade6 } },
    { 0x88,
      0x1A,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBottomLeftGhini, (void*)sub_StateChange_Dojos_ToGreatblade7 } },
    { 0x88,
      0x1B,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_3FTopRightTower7, (void*)sub_StateChange_Dojos_ToGreatblade8 } },
    { 0x88,
      0x1C,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1Entrance, (void*)sub_StateChange_DarkHyruleCastle_B1Entrance } },
    { 0x88,
      0x1D,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_2FBottomRightTower, (void*)sub_StateChange_Dojos_ToGreatblade9 } },
    { 0x88,
      0x1E,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FTopLeftTower, (void*)sub_StateChange_Dojos_ToScarblade0 } },
    { 0x88,
      0x1F,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FThroneRoom,
        (void*)sub_StateChange_DarkHyruleCastle_1FThroneRoom } },
    { 0x88,
      0x20,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FCompass, (void*)sub_StateChange_Dojos_ToScarblade1 } },
    { 0x88,
      0x21,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FTopRightTower, (void*)sub_StateChange_Dojos_ToScarblade2 } },
    { 0x88,
      0x22,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FBeforeThrone, (void*)sub_StateChange_Dojos_ToScarblade3 } },
    { 0x88,
      0x23,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FLoopTopLeft, (void*)sub_StateChange_Dojos_ToScarblade4 } },
    { 0x88,
      0x24,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FLoopTop, (void*)sub_StateChange_Dojos_ToScarblade5 } },
    { 0x88,
      0x25,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FLoopTopRight, (void*)sub_StateChange_Dojos_ToScarblade6 } },
    { 0x88,
      0x26,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FLoopLeft, (void*)sub_StateChange_Dojos_ToScarblade7 } },
    { 0x88,
      0x27,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FLoopRight, (void*)sub_StateChange_Dojos_ToScarblade8 } },
    { 0x88,
      0x28,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FLoopBottomLeft, (void*)sub_StateChange_Dojos_ToScarblade9 } },
    { 0x88,
      0x29,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FLoopBottom, (void*)sub_StateChange_GoronCave_Stairs0 } },
    { 0x88,
      0x2A,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FLoopBottomRight, (void*)sub_StateChange_GoronCave_Stairs1 } },
    { 0x88,
      0x2B,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FBottomLeftTower, (void*)sub_StateChange_GoronCave_Stairs2 } },
    { 0x88,
      0x2C,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_1FBottomRightTower, (void*)sub_StateChange_GoronCave_Stairs3 } },
    { 0x88,
      0x2D,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1BelowThrone, (void*)sub_StateChange_GoronCave_Stairs4 } },
    { 0x88,
      0x2E,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1BelowCompass, (void*)sub_StateChange_GoronCave_Stairs5 } },
    { 0x88,
      0x2F,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1BeforeThrone, (void*)sub_StateChange_GoronCave_Stairs6 } },
    { 0x88,
      0x30,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1ToPrison, (void*)sub_StateChange_GoronCave_Stairs7 } },
    { 0x88,
      0x31,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1BombWall, (void*)sub_StateChange_GoronCave_Stairs8 } },
    { 0x88,
      0x32,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1Keatons, (void*)sub_StateChange_GoronCave_Stairs9 } },
    { 0x88,
      0x33,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1ToPrisonFirebar, (void*)sub_StateChange_Ruins_Beanstalk0 } },
    { 0x88, 0x34, { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1Cannons, (void*)sub_StateChange_Ruins_Beanstalk1 } },
    { 0x88,
      0x35,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1Left, (void*)sub_StateChange_DarkHyruleCastle_B1Left } },
    { 0x88, 0x36, { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1Right, (void*)sub_StateChange_Ruins_Beanstalk2 } },
    { 0x88,
      0x37,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B1Map, (void*)sub_StateChange_DarkHyruleCastle_B1Map } },
    { 0x88,
      0x38,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B2ToPrison, (void*)sub_StateChange_Ruins_Beanstalk3 } },
    { 0x88,
      0x39,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B2Prison, (void*)sub_StateChange_DarkHyruleCastle_B2Prison } },
    { 0x88,
      0x3A,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastle_B2Dropdown, (void*)sub_StateChange_Ruins_Beanstalk4 } },
    { 0x89,
      0x00,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_ZeldaStatuePlatform,
        (void*)sub_StateChange_DarkHyruleCastleOutside_ZeldaStatuePlatform } },
    { 0x89,
      0x01,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_Garden,
        (void*)sub_StateChange_DarkHyruleCastleOutside_Garden } },
    { 0x89,
      0x02,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_OutsideNorthwest,
        (void*)sub_StateChange_Dojos_ToGrimblade0 } },
    { 0x89,
      0x03,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_OutsideNortheast,
        (void*)sub_StateChange_Dojos_ToGrimblade1 } },
    { 0x89,
      0x04,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_OutsideEast, (void*)sub_StateChange_Dojos_ToGrimblade2 } },
    { 0x89,
      0x05,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_OutsideSouthwest,
        (void*)sub_StateChange_Dojos_ToGrimblade3 } },
    { 0x89,
      0x06,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_OutsideSouth, (void*)sub_StateChange_Dojos_ToGrimblade4 } },
    { 0x89,
      0x07,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_OutsideSoutheast,
        (void*)sub_StateChange_Dojos_ToGrimblade5 } },
    { 0x89,
      0x08,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleOutside_8, (void*)sub_StateChange_Dojos_ToGrimblade6 } },
    { 0x8A, 0x00, { NULL, NULL, (void*)sub_unk3_VaatisArms_First, (void*)sub_StateChange_VaatisArms_First } },
    { 0x8A, 0x01, { NULL, NULL, (void*)sub_unk3_VaatisArms_Second, (void*)sub_StateChange_VaatisArms_Second } },
    { 0x8B, 0x00, { NULL, NULL, (void*)sub_unk3_Vaati3_Main, (void*)sub_StateChange_Vaati3_Main } },
    { 0x8B, 0x01, { NULL, NULL, (void*)sub_unk3_Vaati3_Main, (void*)sub_StateChange_Vaati3_Main } },
    { 0x8C, 0x00, { NULL, NULL, (void*)sub_unk3_Vaati2_Main, (void*)sub_StateChange_Vaati2_Main } },
    { 0x8D,
      0x00,
      { NULL, NULL, (void*)sub_unk3_DarkHyruleCastleBridge_Main, (void*)sub_StateChange_DarkHyruleCastleBridge_Main } },
    { 0x8F, 0x00, { NULL, NULL, (void*)sub_unk3_8F_Main, (void*)sub_StateChange_8F_Main } },
};

#define ROOM_FUNC_TABLE_SIZE (sizeof(sRoomFuncTable) / sizeof(sRoomFuncTable[0]))

/* Look up a room function property (4-7) for the given area/room. */
void* Port_GetRoomFuncProp(u32 area, u32 room, u32 prop_idx) {
    if (prop_idx < 4 || prop_idx > 7)
        return NULL;
    for (u32 i = 0; i < ROOM_FUNC_TABLE_SIZE; i++) {
        if (sRoomFuncTable[i].area == area && sRoomFuncTable[i].room == room) {
            return sRoomFuncTable[i].props[prop_idx - 4];
        }
    }
    return NULL;
}

#endif /* PC_PORT */
