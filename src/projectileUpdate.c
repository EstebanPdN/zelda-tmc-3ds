#include "entity.h"
#include "asm.h"
#include "projectile.h"

typedef void (*ProjectileFn)(Entity*);

#ifdef PC_PORT
const ProjectileFn gProjectileFunctions[] = {
    DarkNutSwordSlash,
    RockProjectile,
    BoneProjectile,
    MoblinSpear,
    DekuSeedProjectile,
    Projectile5,
    DirtBallProjectile,
    WindProjectile,
    FireProjectile,
    IceProjectile,
    (ProjectileFn)GleerokProjectile,
    KeatonDagger,
    GuardLineOfSight,
    ArrowProjectile,
    (ProjectileFn)MazaalEnergyBeam,
    (ProjectileFn)OctorokBossProjectile,
    StalfosProjectile,
    LakituCloudProjectile,
    (ProjectileFn)LakituLightning,
    (ProjectileFn)MandiblesProjectile,
    (ProjectileFn)RemovableDust,
    (ProjectileFn)SpiderWeb,
    TorchTrapProjectile,
    GuruguruBarProjectile,
    (ProjectileFn)V1DarkMagicProjectile,
    (ProjectileFn)BallAndChain,
    (ProjectileFn)V1FireProjectile,
    CannonballProjectile,
    (ProjectileFn)V1EyeLaser,
    Winder,
    SpikedRollers,
    (ProjectileFn)V2Projectile,
    V3HandProjectile,
    (ProjectileFn)V3ElectricProjectile,
    (ProjectileFn)GyorgTail,
    GyorgMaleEnergyProjectile,
    V3TennisBallProjectile,
};
#else
extern const ProjectileFn gProjectileFunctions[];
#endif

extern bool32 ProjectileInit(Entity*);

void ProjectileUpdate(Entity* this) {
    if (!(this->flags & ENT_DID_INIT)) {
        if (!ProjectileInit(this)) {
            DeleteThisEntity();
            return;
        }
    } else {
        if (EntityDisabled(this)) {
            DrawEntity(this);
            return;
        }
        sub_080028E0(this);
    }

    gProjectileFunctions[this->id](this);
    this->contactFlags &= ~CONTACT_NOW;
    DrawEntity(this);
}
