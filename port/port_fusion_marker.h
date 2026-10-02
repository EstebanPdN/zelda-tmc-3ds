#ifndef PORT_FUSION_MARKER_H
#define PORT_FUSION_MARKER_H

#include "flags.h"
#include "kinstone.h"

typedef struct PortFusionMarkerReward {
    u32 bank;
    u32 flag;
    bool32 valid;
} PortFusionMarkerReward;

/* The EU/JP retail WorldEvent twins label the five beanstalks and Gina's
 * grave as CND_0, so retail never retires their markers after collecting the
 * reward. The fat port's USA-baseline twin retains CND_5..CND_10 at the same
 * world-event indices. Use only those six baseline special conditions as a
 * cross-region completion hint; all other conditions remain region-native. */
static inline u32 Port_SelectFusionMarkerCondition(u32 activeCondition, u32 baselineCondition) {
    if (baselineCondition >= CND_5 && baselineCondition <= CND_10) {
        return baselineCondition;
    }
    return activeCondition;
}

static inline PortFusionMarkerReward Port_FusionMarkerRewardForCondition(u32 condition) {
    PortFusionMarkerReward reward = { 0, 0, FALSE };
    switch (condition) {
        case CND_5:
            reward.bank = LOCAL_BANK_3;
            reward.flag = SORA_10_H00;
            break;
        case CND_6:
            reward.bank = LOCAL_BANK_3;
            reward.flag = SORA_11_H00;
            break;
        case CND_7:
            reward.bank = LOCAL_BANK_3;
            reward.flag = SORA_12_T00;
            break;
        case CND_8:
            reward.bank = LOCAL_BANK_3;
            reward.flag = SORA_13_H00;
            break;
        case CND_9:
            reward.bank = LOCAL_BANK_3;
            reward.flag = SORA_14_T00;
            break;
        case CND_10:
            reward.bank = LOCAL_BANK_4;
            reward.flag = KS_B15;
            break;
        default:
            return reward;
    }
    reward.valid = TRUE;
    return reward;
}

#endif /* PORT_FUSION_MARKER_H */
