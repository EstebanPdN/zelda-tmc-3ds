#ifndef OCTOROKBOSS_H
#define OCTOROKBOSS_H

#include "enemy.h"

typedef struct OctorokBossEntity OctorokBossEntity;
typedef struct {
    /*0x00*/ u8 unk_0; // [0,1,2,4]  is later stored in super->subtimer
    /*0x01*/ u8 tailCount;
    /*0x02*/ u8 field_0x2;               // [0,1]
    /*0x03*/ u8 targetAngle;             // relates to this->field_0x7a.HALF.HI
    /*0x04*/ u8 rotation;                // [0,1,0xff]
    /*0x05*/ u8 phase4PrevAttackPattern; // [0-4], sets this->phase4AttackPattern
    /*0x06*/ u8 fallingStonesTimer;
    /*0x07*/ u8 field_0x7; // some sort of counter that is only set when hit for the first time?
    /*0x08*/ OctorokBossEntity* mouthObject;
    /*0x0c*/ OctorokBossEntity* tailObjects[5];
    /*0x20*/ OctorokBossEntity* legObjects[4];
} OctorokBossHeap;
PORT_STATIC_ASSERT_SIZE(OctorokBossHeap, 0x30, 0x58, "OctorokBossHeap size incorrect");
PORT_STATIC_ASSERT_OFFSET(OctorokBossHeap, mouthObject, 0x08, 0x08, "OctorokBossHeap mouth offset incorrect");
PORT_STATIC_ASSERT_OFFSET(OctorokBossHeap, tailObjects, 0x0c, 0x10, "OctorokBossHeap tails offset incorrect");
PORT_STATIC_ASSERT_OFFSET(OctorokBossHeap, legObjects, 0x20, 0x38, "OctorokBossHeap legs offset incorrect");

struct OctorokBossEntity {
    /*0x00*/ Entity base;
    /*0x68*/ union SplitHWord field_0x68;
    /*0x6a*/ union SplitHWord field_0x6a;
    /*0x6c*/ union SplitHWord field_0x6c;
    /*0x6e*/ union SplitHWord field_0x6e;
    /*0x70*/ union SplitWord field_0x70;
    /*0x74*/ u16 unk_74;
    /*0x76*/ u16 unk_76;
    /*0x78*/ u8 attackWaitTurns;     /**< Turns until the next attack */
    /*0x79*/ u8 timer;               /**< Reused timer */
    /*0x7a*/ union SplitHWord angle; /**< Angle of legs */
    /*0x7c*/ u8 bossPhase;           /**< Boss Phase
          0: unfrozen
          1: frozen 1
          2: unfrozen
          3: frozen 2
          4: unfrozen -> death */
    /*0x7d*/ u8 currentAttack;
    /*0x7e*/ u8 nextAttackIndex;
    /*0x7f*/ u8 unk_7f;
    /*0x80*/ u8 unk_80;
    /*0x81*/ u8 phase4AttackPattern; /**< Which attack pattern is currently used in phase 4 */
    /*0x82*/ union SplitHWord angularSpeed;
    /*0x84*/ OctorokBossHeap* heap; /**< Heap data allocated with #zMalloc. */
};
PORT_STATIC_ASSERT_SIZE(OctorokBossEntity, 0x88, 0xB8, "OctorokBossEntity size incorrect");
PORT_STATIC_ASSERT_OFFSET(OctorokBossEntity, unk_74, 0x74, 0x9C, "OctorokBossEntity unk_74 offset incorrect");
PORT_STATIC_ASSERT_OFFSET(OctorokBossEntity, attackWaitTurns, 0x78, 0xA0,
                          "OctorokBossEntity attack timer offset incorrect");
PORT_STATIC_ASSERT_OFFSET(OctorokBossEntity, heap, 0x84, 0xB0, "OctorokBossEntity heap offset incorrect");

enum OctorokRotation { ROTATION_CW, ROTATION_CCW, NO_ROTATION = 0xff };

enum OctorokBossPart { WHOLE, LEG_BR, LEG_FR, LEG_FL, LEG_BL, MOUTH, TAIL_END, TAIL };
enum OctorokBossAction {
    INIT,    // 0
    ACTION1, // 1
    HIT,     // 2
    INTRO,   // 3
    BURNING, // 4
};

enum OctorokBossAttack {
    ATTACK_SPITROCK,      // 0
    ATTACK_VACUUM,        // 1
    ATTACK_SMOKE,         // 2
    ATTACK_FREEZE,        // 3
    NO_ATTACK,            // 4
    END_OF_ATTACK_PATTERN // 5
};

enum OctorokBossAction1SubAction {
    ACTION1_AIMTOWARDSPLAYER, // Moving around with step sounds
    ACTION1_WAITFORTURN,      // Also step sounds
    ACTION1_SUBACTION2,       // Step sounds, some kind of attack that is started in OctorokBoss_StartRegularAttack?
    ACTION1_WAITFORATTACK,    // Wait for GET_TIMER(), then OctorokBoss_SetWaitTurnsForNextAttack
    ACTION1_ATTACK,           // Attack
};

#endif // OCTOROKBOSS_H
