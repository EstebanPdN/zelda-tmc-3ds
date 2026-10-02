/**
 * @file cutsceneOrchestrator.c
 * @ingroup Objects
 *
 * @brief Cutscene Orchestrator object
 */
#include "entity.h"
#include "script.h"
#include "hitbox.h"

void CutsceneOrchestrator(Entity* this) {
#ifdef PC_PORT
    extern void Port_TrackOrch(Entity* ent);
    extern void Port_LogOrchScriptPc(Entity* ent);
    Port_TrackOrch(this);
#endif
    if ((this->flags & ENT_SCRIPTED) != 0) {
        if (this->action == 0) {
            this->action = 1;
            this->hitbox = (Hitbox*)&gHitbox_2;
            InitScriptForNPC(this);
        } else {
            ExecuteScriptAndHandleAnimation(this, NULL);
        }
    } else {
        this->action = 1;
    }
#ifdef PC_PORT
    /* After dispatch so sip reflects the next instruction the script
     * will run (or whatever it stopped at). Includes both the no-script
     * idle path and the post-execute path. */
    Port_LogOrchScriptPc(this);
#endif
}
