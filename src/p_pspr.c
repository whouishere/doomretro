/*
========================================================================

                           D O O M  R e t r o
         The classic, refined DOOM source port. For Windows PC.

========================================================================

  Copyright © 1993-2021 by id Software LLC, a ZeniMax Media company.
  Copyright © 2013-2021 by Brad Harding <mailto:brad@doomretro.com>.

  DOOM Retro is a fork of Chocolate DOOM. For a list of credits, see
  <https://github.com/bradharding/doomretro/wiki/CREDITS>.

  This file is a part of DOOM Retro.

  DOOM Retro is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 3 of the License, or (at your
  option) any later version.

  DOOM Retro is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with DOOM Retro. If not, see <https://www.gnu.org/licenses/>.

  DOOM is a registered trademark of id Software LLC, a ZeniMax Media
  company, in the US and/or other countries, and is used without
  permission. All other trademarks are the property of their respective
  holders. DOOM Retro is in no way affiliated with nor endorsed by
  id Software.

========================================================================
*/

#include "doomstat.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "i_gamepad.h"
#include "i_timer.h"
#include "m_config.h"
#include "m_random.h"
#include "p_local.h"
#include "p_tick.h"
#include "s_sound.h"

#define LOWERSPEED  (6 * FRACUNIT)
#define RAISESPEED  (6 * FRACUNIT)

dboolean        autoaim = autoaim_default;
dboolean        centerweapon = centerweapon_default;
int             weaponbob = weaponbob_default;
dboolean        weaponbounce = weaponbounce_default;
dboolean        weaponrecoil = weaponrecoil_default;

uint64_t        stat_shotsfired_fists = 0;
uint64_t        stat_shotsfired_chainsaw = 0;
uint64_t        stat_shotsfired_pistol = 0;
uint64_t        stat_shotsfired_shotgun = 0;
uint64_t        stat_shotsfired_supershotgun = 0;
uint64_t        stat_shotsfired_chaingun = 0;
uint64_t        stat_shotsfired_rocketlauncher = 0;
uint64_t        stat_shotsfired_plasmarifle = 0;
uint64_t        stat_shotsfired_bfg9000 = 0;
uint64_t        stat_shotssuccessful_fists = 0;
uint64_t        stat_shotssuccessful_chainsaw = 0;
uint64_t        stat_shotssuccessful_pistol = 0;
uint64_t        stat_shotssuccessful_shotgun = 0;
uint64_t        stat_shotssuccessful_supershotgun = 0;
uint64_t        stat_shotssuccessful_chaingun = 0;
uint64_t        stat_shotssuccessful_rocketlauncher = 0;
uint64_t        stat_shotssuccessful_plasmarifle = 0;
uint64_t        stat_shotssuccessful_bfg9000 = 0;

dboolean        successfulshot;
dboolean        skippsprinterp;

extern dboolean hitwall;

//
// A_Recoil
//
void A_Recoil(weapontype_t weapon)
{
    if (weaponrecoil)
        viewplayer->recoil = weaponinfo[weapon].recoil;
}

//
// mbf21: P_SetPspritePtr
//
void P_SetPspritePtr(pspdef_t *psp, statenum_t stnum)
{
    do
    {
        if (!stnum)
        {
            // object removed itself
            psp->state = NULL;
            break;
        }
        else
        {
            state_t *state = &states[stnum];

            psp->state = state;
            psp->tics = state->tics;    // could be 0

            if (state->misc1)
            {
                // coordinate set
                psp->sx = state->misc1 << FRACBITS;
                psp->sy = state->misc2 << FRACBITS;
            }

            // Call action routine.
            // Modified handling.
            if (state->action)
            {
                state->action(viewplayer->mo, viewplayer, psp);

                if (!psp->state)
                    break;
            }

            stnum = psp->state->nextstate;
        }
    } while (!psp->tics);           // an initial state of 0 could cycle through
}

//
// P_SetPsprite
//
void P_SetPsprite(size_t position, statenum_t stnum)
{
    P_SetPspritePtr(&viewplayer->psprites[position], stnum);
}

//
// P_EquipWeapon
//
void P_EquipWeapon(weapontype_t weapon)
{
    viewplayer->pendingweapon = weapon;

    if (weaponinfo[weapon].ammotype != weaponinfo[viewplayer->readyweapon].ammotype)
        ammohighlight = I_GetTimeMS() + HUD_AMMO_HIGHLIGHT_WAIT;
}

//
// P_BringUpWeapon
// Starts bringing the pending weapon up
// from the bottom of the screen.
//
static void P_BringUpWeapon(void)
{
    statenum_t  newstate;

    if (viewplayer->pendingweapon == wp_nochange)
        viewplayer->pendingweapon = viewplayer->readyweapon;
    else if (viewplayer->pendingweapon == wp_chainsaw)
        S_StartSound(viewplayer->mo, sfx_sawup);

    newstate = weaponinfo[viewplayer->pendingweapon].upstate;

    viewplayer->pendingweapon = wp_nochange;
    viewplayer->psprites[ps_weapon].sy = WEAPONBOTTOM;

    P_SetPsprite(ps_weapon, newstate);
}

//
// P_CheckAmmo
// Returns true if there is enough ammo to shoot.
// If not, selects the next weapon to use.
//
dboolean P_CheckAmmo(weapontype_t weapon)
{
    ammotype_t  ammotype = weaponinfo[weapon].ammotype;

    // Some do not need ammunition anyway.
    if (ammotype == am_noammo)
        return true;

    // Return if current ammunition sufficient.
    if (viewplayer->ammo[ammotype] >= weaponinfo[weapon].minammo && viewplayer->weaponowned[weapon])
        return true;

    // Out of ammo, pick a weapon to change to.
    // Preferences are set here.
    if (viewplayer->weaponowned[wp_plasma]
        && viewplayer->ammo[am_cell] >= weaponinfo[wp_plasma].minammo)
        P_EquipWeapon(wp_plasma);
    else if (viewplayer->weaponowned[wp_supershotgun]
        && viewplayer->ammo[am_shell] >= weaponinfo[wp_supershotgun].minammo
        && viewplayer->preferredshotgun == wp_supershotgun)
        P_EquipWeapon(wp_supershotgun);
    else if (viewplayer->weaponowned[wp_chaingun]
        && viewplayer->ammo[am_clip] >= weaponinfo[wp_chaingun].minammo)
        P_EquipWeapon(wp_chaingun);
    else if (viewplayer->weaponowned[wp_shotgun]
        && viewplayer->ammo[am_shell] >= weaponinfo[wp_shotgun].minammo)
        P_EquipWeapon(wp_shotgun);
    else if (viewplayer->weaponowned[wp_pistol]
        && viewplayer->ammo[am_clip] >= weaponinfo[wp_pistol].minammo)
        P_EquipWeapon(wp_pistol);
    else if (viewplayer->weaponowned[wp_chainsaw])
        P_EquipWeapon(wp_chainsaw);
    else
        P_EquipWeapon(wp_fist);

    return false;
}

//
// P_SubtractAmmo
//
static void P_SubtractAmmo(void)
{
    ammotype_t  ammotype = weaponinfo[viewplayer->readyweapon].ammotype;

    if (ammotype != am_noammo)
    {
        viewplayer->ammo[ammotype] = MAX(0, viewplayer->ammo[ammotype] - weaponinfo[viewplayer->readyweapon].minammo);
        ammohighlight = I_GetTimeMS() + HUD_AMMO_HIGHLIGHT_WAIT;
    }
}

//
// P_FireWeapon
//
void P_FireWeapon(void)
{
    weapontype_t    readyweapon = viewplayer->readyweapon;

    if (!P_CheckAmmo(readyweapon) || (automapactive && !am_followmode))
        return;

    P_SetPsprite(ps_weapon, weaponinfo[readyweapon].atkstate);

    if (gp_vibrate_weapons)
    {
        if (readyweapon == wp_chainsaw && linetarget)
        {
            I_GamepadVibration(MAX_VIBRATION_STRENGTH);
            weaponvibrationtics = weaponinfo[readyweapon].tics;
        }
        else if (readyweapon != wp_fist)
        {
            I_GamepadVibration(weaponinfo[readyweapon].strength * gp_vibrate_weapons / 100);
            weaponvibrationtics = weaponinfo[readyweapon].tics;
        }
    }

    if (centerweapon)
    {
        pspdef_t    *psp = &viewplayer->psprites[ps_weapon];
        state_t     *state = psp->state;

        if (!state->misc1)
            psp->sx = 0;

        if (!state->misc2)
            psp->sy = WEAPONTOP;
    }
}

//
// P_DropWeapon
// Player died, so put the weapon away.
//
void P_DropWeapon(void)
{
    P_SetPsprite(ps_weapon, weaponinfo[viewplayer->readyweapon].downstate);
}

//
// A_WeaponReady
// The player can fire the weapon
// or change to another weapon at this time.
// Follows after getting weapon up,
// or after previous attack/fire sequence.
//
void A_WeaponReady(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    weapontype_t    readyweapon = player->readyweapon;
    weapontype_t    pendingweapon = player->pendingweapon;

    if (readyweapon == wp_chainsaw && psp->state == &states[S_SAW])
        S_StartSound(actor, sfx_sawidl);

    // check for change
    //  if player is dead, put the weapon away
    if (pendingweapon != wp_nochange || player->health <= 0)
    {
        if (gp_vibrate_weapons)
        {
            if (pendingweapon == wp_chainsaw && !REKKR)
            {
                idlevibrationstrength = CHAINSAW_IDLE_VIBRATION_STRENGTH * gp_vibrate_weapons / 100;
                I_GamepadVibration(idlevibrationstrength);
            }
            else if (idlevibrationstrength)
            {
                idlevibrationstrength = 0;
                I_StopGamepadVibration();
            }
        }

        P_DropWeapon();
        return;
    }

    // check for fire
    //  the missile launcher and BFG do not auto fire
    if (player->cmd.buttons & BT_ATTACK)
    {
        if (!player->attackdown || (readyweapon != wp_missile && readyweapon != wp_bfg))
        {
            player->attackdown = true;
            P_FireWeapon();
        }
    }
    else
        player->attackdown = false;
}

//
// A_ReFire
// The player can re-fire the weapon
// without lowering it entirely.
//
void A_ReFire(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    // check for fire
    //  (if a weapon change is pending, let it go through instead)
    if ((player->cmd.buttons & BT_ATTACK) && player->pendingweapon == wp_nochange && player->health > 0)
    {
        player->refire++;
        P_FireWeapon();
    }
    else
    {
        player->refire = 0;
        P_CheckAmmo(player->readyweapon);
    }
}

void A_CheckReload(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    if (!P_CheckAmmo(player->readyweapon))
        P_SetPsprite(ps_weapon, weaponinfo[player->readyweapon].downstate);
}

//
// A_Lower
// Lowers current weapon,
// and changes weapon at bottom.
//
void A_Lower(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    psp->sy += LOWERSPEED;

    // Is already down.
    if (psp->sy < WEAPONBOTTOM)
        return;

    // Player is dead.
    if (player->playerstate == PST_DEAD)
    {
        psp->sy = WEAPONBOTTOM;
        return;         // don't bring weapon back up
    }

    // The old weapon has been lowered off the screen,
    // so change the weapon and start raising it
    if (player->health <= 0)
    {
        // Player is dead, so keep the weapon off screen.
        P_SetPsprite(ps_weapon, S_NULL);
        return;
    }

    if (player->pendingweapon != wp_nochange)
        player->readyweapon = player->pendingweapon;

    P_BringUpWeapon();
}

//
// A_Raise
//
void A_Raise(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    psp->sy -= RAISESPEED;

    if (psp->sy > WEAPONTOP)
        return;

    psp->sy = WEAPONTOP;
    P_SetPsprite(ps_weapon, weaponinfo[player->readyweapon].readystate);
}

//
// A_GunFlash
//
void A_GunFlash(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    P_SetPsprite(ps_flash, weaponinfo[player->readyweapon].flashstate);
}

//
// WEAPON ATTACKS
//

//
// A_Punch
//
void A_Punch(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    angle_t angle = actor->angle + (M_SubRandom() << 18);
    int     range = viewplayer->mo->info->meleerange;
    int     slope = P_AimLineAttack(actor, angle, range, MF_FRIEND);
    int     damage = (M_Random() % 10 + 1) << 1;

    if (player->powers[pw_strength])
        damage *= 10;

    if (!linetarget)
        slope = P_AimLineAttack(actor, angle, range, 0);

    hitwall = false;
    P_LineAttack(actor, angle, range, slope, damage);

    player->shotsfired[wp_fist]++;
    stat_shotsfired_fists = SafeAdd(stat_shotsfired_fists, 1);

    if (linetarget || hitwall)
    {
        if (!(weaponinfo[player->readyweapon].flags & WPF_SILENT))
            P_NoiseAlert(actor);

        S_StartSound(actor, sfx_punch);

        // turn to face target
        if (linetarget)
        {
            actor->angle = R_PointToAngle2(actor->x, actor->y, linetarget->x, linetarget->y);

            player->shotssuccessful[wp_fist]++;
            stat_shotssuccessful_fists = SafeAdd(stat_shotssuccessful_fists, 1);
        }
    }
}

//
// A_Saw
//
void A_Saw(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    angle_t angle = actor->angle + (M_SubRandom() << 18);
    int     range = viewplayer->mo->info->meleerange + 1;
    int     slope = P_AimLineAttack(actor, angle, range, MF_FRIEND);

    if (!linetarget)
        slope = P_AimLineAttack(actor, angle, range, 0);

    P_LineAttack(actor, angle, range, slope, 2 * (M_Random() % 10 + 1));
    A_Recoil(wp_chainsaw);

    if (!(weaponinfo[player->readyweapon].flags & WPF_SILENT))
        P_NoiseAlert(actor);

    player->shotsfired[wp_chainsaw]++;
    stat_shotsfired_chainsaw = SafeAdd(stat_shotsfired_chainsaw, 1);

    if (!linetarget)
    {
        S_StartSound(actor, sfx_sawful);
        return;
    }

    player->shotssuccessful[wp_chainsaw]++;
    stat_shotssuccessful_chainsaw = SafeAdd(stat_shotssuccessful_chainsaw, 1);

    S_StartSound(actor, sfx_sawhit);

    // turn to face target
    angle = R_PointToAngle2(actor->x, actor->y, linetarget->x, linetarget->y);

    if (angle - actor->angle > ANG180)
    {
        if ((int)(angle - actor->angle) < -ANG90 / 20)
            actor->angle = angle + ANG90 / 21;
        else
            actor->angle -= ANG90 / 20;
    }
    else if (angle - actor->angle > ANG90 / 20)
        actor->angle = angle - ANG90 / 21;
    else
        actor->angle += ANG90 / 20;

    actor->flags |= MF_JUSTATTACKED;
}

//
// A_FireMissile
//
void A_FireMissile(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    P_SubtractAmmo();
    P_SpawnPlayerMissile(actor, MT_ROCKET);

    player->shotsfired[wp_missile]++;
    stat_shotsfired_rocketlauncher = SafeAdd(stat_shotsfired_rocketlauncher, 1);
}

//
// A_FireBFG
//
void A_FireBFG(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    P_SubtractAmmo();
    P_SpawnPlayerMissile(actor, MT_BFG);
}

//
// A_FireOldBFG
//
// This function emulates DOOM's Pre-Beta BFG
// By Lee Killough 06/06/98, 07/11/98, 07/19/98, 08/20/98
//
// This code may not be used in other mods without appropriate credit given.
// Code leeches will be telefragged.
//
void A_FireOldBFG(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    mobjtype_t  type = MT_PLASMA1;

    P_SubtractAmmo();

    player->extralight = 2;

    do
    {
        mobj_t  *th;
        angle_t an = actor->angle;
        angle_t an1 = ((M_Random() & 127) - 64) * (ANG90 / 768) + an;
        angle_t an2 = ((M_Random() & 127) - 64) * (ANG90 / 640) + ANG90;
        fixed_t slope;

        if (usemouselook && !autoaim)
            slope = PLAYERSLOPE(player);
        else
        {
            // killough 08/02/98: make autoaiming prefer enemies
            int mask = MF_FRIEND;

            do
            {
                slope = P_AimLineAttack(actor, an, 16 * 64 * FRACUNIT, mask);

                if (!linetarget)
                {
                    slope = P_AimLineAttack(actor, (an += (1 << 26)), 16 * 64 * FRACUNIT, mask);

                    if (!linetarget)
                    {
                        slope = P_AimLineAttack(actor, (an -= (2 << 26)), 16 * 64 * FRACUNIT, mask);

                        if (!linetarget)
                        {
                            an = actor->angle;
                            slope = (usemouselook ? PLAYERSLOPE(player) : 0);
                        }
                    }
                }
            } while (mask && (mask = 0, !linetarget));  // killough 08/02/98
        }

        an1 += an - actor->angle;

        // [crispy] consider negative slope
        if (slope < 0)
            an2 -= tantoangle[-slope >> DBITS];
        else
            an2 += tantoangle[slope >> DBITS];

        th = P_SpawnMobj(actor->x, actor->y, actor->z + 62 * FRACUNIT - player->psprites[ps_weapon].sy, type);
        P_SetTarget(&th->target, actor);
        th->angle = an1;
        th->momx = finecosine[an1 >> ANGLETOFINESHIFT] * 25;
        th->momy = finesine[an1 >> ANGLETOFINESHIFT] * 25;
        th->momz = finetangent[an2 >> ANGLETOFINESHIFT] * 25;

        // [crispy] suppress interpolation of player missiles for the first tic
        th->interpolate = -1;

        P_CheckMissileSpawn(th);
    } while (type != MT_PLASMA2 && (type = MT_PLASMA2));  // killough: obfuscated!

    A_Recoil(wp_bfg);
}

//
// A_FirePlasma
//
void A_FirePlasma(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    P_SubtractAmmo();
    P_SetPsprite(ps_flash, weaponinfo[player->readyweapon].flashstate + (M_Random() & 1));
    P_SpawnPlayerMissile(actor, MT_PLASMA);

    player->shotsfired[wp_plasma]++;
    stat_shotsfired_plasmarifle = SafeAdd(stat_shotsfired_plasmarifle, 1);
}

//
// P_BulletSlope
// Sets a slope so a near miss is at approximately
// the height of the intended target
//
static fixed_t  bulletslope;

static void P_BulletSlope(mobj_t *actor)
{
    if (usemouselook && !autoaim)
        bulletslope = PLAYERSLOPE(viewplayer);
    else
    {
        // killough 08/02/98: make autoaiming prefer enemies
        int mask = MF_FRIEND;

        do
        {
            angle_t an = actor->angle;

            // see which target is to be aimed at
            bulletslope = P_AimLineAttack(actor, an, 16 * 64 * FRACUNIT, mask);

            if (!linetarget)
            {
                bulletslope = P_AimLineAttack(actor, (an += (1 << 26)), 16 * 64 * FRACUNIT, mask);

                if (!linetarget)
                {
                    bulletslope = P_AimLineAttack(actor, (an -= (2 << 26)), 16 * 64 * FRACUNIT, mask);

                    if (!linetarget && usemouselook)
                        bulletslope = PLAYERSLOPE(viewplayer);
                }
            }
        } while (mask && (mask = 0, !linetarget));  // killough 08/02/98
    }
}

//
// P_GunShot
//
static void P_GunShot(mobj_t *actor, dboolean accurate)
{
    angle_t angle = actor->angle;

    if (!accurate)
        angle += M_SubRandom() << 18;

    P_LineAttack(actor, angle, MISSILERANGE, bulletslope, 5 * (M_Random() % 3 + 1));
}

//
// A_FirePistol
//
void A_FirePistol(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    if (!(weaponinfo[player->readyweapon].flags & WPF_SILENT))
        P_NoiseAlert(actor);

    S_StartSound(actor, sfx_pistol);
    P_SubtractAmmo();
    P_SetPsprite(ps_flash, weaponinfo[player->readyweapon].flashstate);
    P_BulletSlope(actor);

    successfulshot = false;

    P_GunShot(actor, !player->refire);
    A_Recoil(wp_pistol);

    player->shotsfired[wp_pistol]++;
    stat_shotsfired_pistol = SafeAdd(stat_shotsfired_pistol, 1);

    if (successfulshot)
    {
        player->shotssuccessful[wp_pistol]++;
        stat_shotssuccessful_pistol = SafeAdd(stat_shotssuccessful_pistol, 1);
    }
}

//
// A_FireShotgun
//
void A_FireShotgun(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    if (!(weaponinfo[player->readyweapon].flags & WPF_SILENT))
        P_NoiseAlert(actor);

    S_StartSound(actor, sfx_shotgn);
    P_SubtractAmmo();
    P_SetPsprite(ps_flash, weaponinfo[player->readyweapon].flashstate);
    P_BulletSlope(actor);

    successfulshot = false;

    for (int i = 0; i < 7; i++)
        P_GunShot(actor, false);

    A_Recoil(wp_shotgun);

    player->shotsfired[wp_shotgun]++;
    stat_shotsfired_shotgun = SafeAdd(stat_shotsfired_shotgun, 1);

    if (successfulshot)
    {
        player->shotssuccessful[wp_shotgun]++;
        stat_shotssuccessful_shotgun = SafeAdd(stat_shotssuccessful_shotgun, 1);
    }

    player->preferredshotgun = wp_shotgun;
}

//
// A_FireShotgun2
//
void A_FireShotgun2(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    if (!(weaponinfo[player->readyweapon].flags & WPF_SILENT))
        P_NoiseAlert(actor);

    S_StartSound(actor, sfx_dshtgn);
    P_SubtractAmmo();
    P_SetPsprite(ps_flash, weaponinfo[player->readyweapon].flashstate);
    P_BulletSlope(actor);

    successfulshot = false;

    for (int i = 0; i < 20; i++)
        P_LineAttack(actor, actor->angle + (M_SubRandom() << ANGLETOFINESHIFT), MISSILERANGE,
            bulletslope + (M_SubRandom() << 5), 5 * (M_Random() % 3 + 1));

    A_Recoil(wp_supershotgun);

    player->shotsfired[wp_supershotgun]++;
    stat_shotsfired_supershotgun = SafeAdd(stat_shotsfired_supershotgun, 1);

    if (successfulshot)
    {
        player->shotssuccessful[wp_supershotgun]++;
        stat_shotssuccessful_supershotgun = SafeAdd(stat_shotssuccessful_supershotgun, 1);
    }

    player->preferredshotgun = wp_supershotgun;
}

void A_OpenShotgun2(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    S_StartSound(actor, sfx_dbopn);
}

void A_LoadShotgun2(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    S_StartSound(actor, sfx_dbload);
}

void A_CloseShotgun2(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    S_StartSound(actor, sfx_dbcls);
    A_ReFire(actor, player, psp);
}

//
// A_FireCGun
//
void A_FireCGun(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    // [BH] Fix <https://doomwiki.org/wiki/Chaingun_makes_two_sounds_firing_single_bullet>.
    if (!player->ammo[weaponinfo[player->readyweapon].ammotype])
        return;

    S_StartSound(actor, sfx_pistol);

    if (!(weaponinfo[player->readyweapon].flags & WPF_SILENT))
        P_NoiseAlert(actor);

    P_SubtractAmmo();
    P_SetPsprite(ps_flash, weaponinfo[player->readyweapon].flashstate + (unsigned int)((psp->state - &states[S_CHAIN1]) & 1));
    P_BulletSlope(actor);

    successfulshot = false;

    P_GunShot(actor, !player->refire);
    A_Recoil(wp_chaingun);

    player->shotsfired[wp_chaingun]++;
    stat_shotsfired_chaingun = SafeAdd(stat_shotsfired_chaingun, 1);

    if (successfulshot)
    {
        player->shotssuccessful[wp_chaingun]++;
        stat_shotssuccessful_chaingun = SafeAdd(stat_shotssuccessful_chaingun, 1);
    }
}

void A_Light0(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    if (player)
        player->extralight = 0;
}

void A_Light1(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    if (player)
        player->extralight = 1;
}

void A_Light2(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    if (player)
        player->extralight = 2;
}

//
// A_BFGSpray
// Spawn a BFG explosion on every monster in view
//
void A_BFGSpray(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    mobj_t  *mo = actor->target;
    angle_t an = mo->angle - ANG90 / 2;

    if (!(weaponinfo[player->readyweapon].flags & WPF_SILENT))
        P_NoiseAlert(actor);

    // offset angles from its attack angle
    for (int i = 0; i < 40; i++)
    {
        int damage = 15;

        // killough 08/02/98: make autoaiming prefer enemies
        if (P_AimLineAttack(mo, an, 16 * 64 * FRACUNIT, MF_FRIEND), !linetarget)
            P_AimLineAttack(mo, an, 16 * 64 * FRACUNIT, 0);

        an += ANG90 / 40;

        if (!linetarget)
            continue;

        successfulshot = true;

        P_SpawnMobj(linetarget->x, linetarget->y, linetarget->z + (linetarget->height >> 2), MT_EXTRABFG);

        for (int j = 0; j < 15; j++)
            damage += M_Random() & 7;

        P_DamageMobj(linetarget, mo, mo, damage, true);
    }

    if (mo->player)
    {
        mo->player->shotsfired[wp_bfg]++;
        stat_shotsfired_bfg9000 = SafeAdd(stat_shotsfired_bfg9000, 1);

        if (successfulshot)
        {
            mo->player->shotssuccessful[wp_bfg]++;
            stat_shotssuccessful_bfg9000 = SafeAdd(stat_shotssuccessful_bfg9000, 1);
        }
    }

    successfulshot = false;
}

//
// A_BFGSound
//
void A_BFGSound(mobj_t *actor, player_t *player, pspdef_t *psp)
{
    S_StartSound(actor, sfx_bfg);
}

//
// P_SetupPsprites
// Called at start of level for each player.
//
void P_SetupPsprites(void)
{
    // remove all psprites
    viewplayer->psprites[ps_weapon].state = NULL;
    viewplayer->psprites[ps_weapon].tics = -1;
    viewplayer->psprites[ps_flash].state = NULL;
    viewplayer->psprites[ps_flash].tics = -1;

    // spawn the gun
    viewplayer->pendingweapon = viewplayer->readyweapon;
    P_BringUpWeapon();

    if (r_playersprites)
        skippsprinterp = true;
}

//
// P_MovePsprites
// Called every tic by player thinking routine.
//
void P_MovePsprites(void)
{
    pspdef_t    *psp = viewplayer->psprites;
    pspdef_t    *weapon = &psp[ps_weapon];
    pspdef_t    *flash = &psp[ps_flash];

    if (weapon->tics != -1 && !--weapon->tics)
        P_SetPsprite(ps_weapon, weapon->state->nextstate);

    if (flash->tics != -1 && !--flash->tics)
        P_SetPsprite(ps_flash, flash->state->nextstate);

    if (weapon->state->action == &A_WeaponReady)
    {
        // bob the weapon based on movement speed
        fixed_t momx = viewplayer->momx;
        fixed_t momy = viewplayer->momy;
        fixed_t bob = MAXBOB * stillbob / 400;

        if (momx | momy)
            bob = MAX(MIN((FixedMul(momx, momx) + FixedMul(momy, momy)) >> 2, MAXBOB) * weaponbob / 100, bob);

        // [BH] smooth out weapon bob by zeroing out really small bobs
        if (bob < FRACUNIT / 2)
        {
            weapon->sx = 0;
            weapon->sy = WEAPONTOP;
        }
        else
        {
            int angle = (128 * leveltime) & FINEMASK;

            weapon->sx = FixedMul(bob, finecosine[angle]);
            weapon->sy = WEAPONTOP + FixedMul(bob, finesine[angle & (FINEANGLES / 2 - 1)]);
        }
    }

    // [BH] shake the BFG before firing when weapon recoil enabled
    if (viewplayer->readyweapon == wp_bfg && weaponrecoil)
    {
        if (weapon->state == &states[S_BFG1])
        {
            weapon->sx = M_RandomInt(-2, 2) * FRACUNIT;
            weapon->sy = WEAPONTOP + M_RandomInt(-1, 1) * FRACUNIT;
        }
        else if (weapon->state == &states[S_BFG2])
        {
            weapon->sx = 0;
            weapon->sy = WEAPONTOP;
        }
    }

    flash->sx = weapon->sx;
    flash->sy = weapon->sy;
}

//
// [XA] New mbf21 codepointers
//

//
// A_WeaponProjectile
// A parameterized player weapon projectile attack. Does not consume ammo.
//   args[0]: Type of actor to spawn
//   args[1]: Angle (degrees, in fixed point), relative to calling player's angle
//   args[2]: Pitch (degrees, in fixed point), relative to calling player's pitch; approximated
//   args[3]: X/Y spawn offset, relative to calling player's angle
//   args[4]: Z spawn offset, relative to player's default projectile fire height
//
void A_WeaponProjectile(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  int type, angle, pitch, spawnofs_xy, spawnofs_z;
  mobj_t *mo;
  int an;

  if (!psp->state || !psp->state->args[0])
    return;

  type        = psp->state->args[0] - 1;
  angle       = psp->state->args[1];
  pitch       = psp->state->args[2];
  spawnofs_xy = psp->state->args[3];
  spawnofs_z  = psp->state->args[4];

  mo = P_SpawnPlayerMissile(player->mo, type);
  if (!mo)
    return;

  // adjust angle
  mo->angle += (angle_t)(((int64_t)angle << 16) / 360);
  an = mo->angle >> ANGLETOFINESHIFT;
  mo->momx = FixedMul(mo->info->speed, finecosine[an]);
  mo->momy = FixedMul(mo->info->speed, finesine[an]);

  // adjust pitch (approximated, using Doom's ye olde
  // finetangent table; same method as autoaim)
  mo->momz += FixedMul(mo->info->speed, DegToSlope(pitch));

  // adjust position
  an = (player->mo->angle - ANG90) >> ANGLETOFINESHIFT;
  mo->x += FixedMul(spawnofs_xy, finecosine[an]);
  mo->y += FixedMul(spawnofs_xy, finesine[an]);
  mo->z += spawnofs_z;

  // set tracer to the player's autoaim target,
  // so player seeker missiles prioritizing the
  // baddie the player is actually aiming at. ;)
  mo->tracer = linetarget;
}

//
// A_WeaponBulletAttack
// A parameterized player weapon bullet attack. Does not consume ammo.
//   args[0]: Horizontal spread (degrees, in fixed point)
//   args[1]: Vertical spread (degrees, in fixed point)
//   args[2]: Number of bullets to fire; if not set, defaults to 1
//   args[3]: Base damage of attack (e.g. for 5d3, customize the 5); if not set, defaults to 5
//   args[4]: Attack damage modulus (e.g. for 5d3, customize the 3); if not set, defaults to 3
//
void A_WeaponBulletAttack(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  int hspread, vspread, numbullets, damagebase, damagemod;
  int i, damage, angle, slope;

  if (!psp->state)
    return;

  hspread    = psp->state->args[0];
  vspread    = psp->state->args[1];
  numbullets = psp->state->args[2];
  damagebase = psp->state->args[3];
  damagemod  = psp->state->args[4];

  P_BulletSlope(player->mo);

  for (i = 0; i < numbullets; i++)
  {
    damage = (M_Random() % damagemod + 1) * damagebase;
    angle = (int)player->mo->angle + P_RandomHitscanAngle(hspread);
    slope = bulletslope + P_RandomHitscanSlope(vspread);

    P_LineAttack(player->mo, angle, MISSILERANGE, slope, damage);
  }
}

//
// A_WeaponMeleeAttack
// A parameterized player weapon melee attack.
//   args[0]: Base damage of attack (e.g. for 2d10, customize the 2); if not set, defaults to 2
//   args[1]: Attack damage modulus (e.g. for 2d10, customize the 10); if not set, defaults to 10
//   args[2]: Berserk damage multiplier (fixed point); if not set, defaults to 1.0 (no change).
//   args[3]: Sound to play if attack hits
//   args[4]: Range (fixed point); if not set, defaults to player mobj's melee range
//
void A_WeaponMeleeAttack(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  int damagebase, damagemod, zerkfactor, hitsound, range;
  angle_t angle;
  int t, slope, damage;

  if (!psp->state)
    return;

  damagebase = psp->state->args[0];
  damagemod  = psp->state->args[1];
  zerkfactor = psp->state->args[2];
  hitsound   = psp->state->args[3];
  range      = psp->state->args[4];

  if (range == 0)
    range = player->mo->info->meleerange;

  damage = (M_Random() % damagemod + 1) * damagebase;
  if (player->powers[pw_strength])
    damage = (damage * zerkfactor) >> FRACBITS;

  // slight randomization; weird vanillaism here. :P
  angle = player->mo->angle;

  t = M_Random();
  angle += (t - M_Random())<<18;

  // make autoaim prefer enemies
  slope = P_AimLineAttack(player->mo, angle, range, MF_FRIEND);
  if (!linetarget)
    slope = P_AimLineAttack(player->mo, angle, range, 0);

  // attack, dammit!
  P_LineAttack(player->mo, angle, range, slope, damage);

  // missed? ah, welp.
  if (!linetarget)
    return;

  // un-missed!
  S_StartSound(player->mo, hitsound);

  // turn to face target
  player->mo->angle = R_PointToAngle2(player->mo->x, player->mo->y, linetarget->x, linetarget->y);
}

//
// A_WeaponSound
// Plays a sound. Usable from weapons, unlike A_PlaySound
//   args[0]: ID of sound to play
//   args[1]: If 1, play sound at full volume (may be useful in DM?)
//
void A_WeaponSound(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  if (!psp->state)
    return;

  S_StartSound(psp->state->args[1] ? NULL : player->mo, psp->state->args[0]);
}

//
// A_WeaponAlert
// Alerts monsters to the player's presence. Handy when combined with WPF_SILENT.
//
void A_WeaponAlert(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  P_NoiseAlert(player->mo);
}

//
// A_WeaponJump
// Jumps to the specified state, with variable random chance.
// Basically the same as A_RandomJump, but for weapons.
//   args[0]: State number
//   args[1]: Chance, out of 255, to make the jump
//
void A_WeaponJump(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  if (!psp->state)
    return;

  if (M_Random() < psp->state->args[1])
    P_SetPspritePtr(psp, psp->state->args[0]);
}

//
// A_ConsumeAmmo
// Subtracts ammo from the player's "inventory". 'Nuff said.
//   args[0]: Amount of ammo to consume. If zero, use the weapon's ammo-per-shot amount.
//
void A_ConsumeAmmo(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  int amount;
  ammotype_t type;

  // don't do dumb things, kids
  type = weaponinfo[player->readyweapon].ammotype;
  if (!psp->state || type == am_noammo)
    return;

  // use the weapon's ammo-per-shot amount if zero.
  // to subtract zero ammo, don't call this function. ;)
  if (psp->state->args[0] != 0)
    amount = psp->state->args[0];
  else
    amount = weaponinfo[player->readyweapon].minammo;

  // subtract ammo, but don't let it get below zero
  if (player->ammo[type] >= amount)
    player->ammo[type] -= amount;
  else
    player->ammo[type] = 0;
}

//
// A_CheckAmmo
// Jumps to a state if the player's ammo is lower than the specified amount.
//   args[0]: State to jump to
//   args[1]: Minimum required ammo to NOT jump. If zero, use the weapon's ammo-per-shot amount.
//
void A_CheckAmmo(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  int amount;
  ammotype_t type;

  type = weaponinfo[player->readyweapon].ammotype;
  if (!psp->state || type == am_noammo)
    return;

  if (psp->state->args[1] != 0)
    amount = psp->state->args[1];
  else
    amount = weaponinfo[player->readyweapon].minammo;

  if (player->ammo[type] < amount)
    P_SetPspritePtr(psp, psp->state->args[0]);
}

//
// A_RefireTo
// Jumps to a state if the player is holding down the fire button
//   args[0]: State to jump to
//   args[1]: If nonzero, skip the ammo check
//
void A_RefireTo(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  if (!psp->state)
    return;

  if ((psp->state->args[1] || P_CheckAmmo(player->readyweapon))
  &&  (player->cmd.buttons & BT_ATTACK)
  &&  (player->pendingweapon == wp_nochange && player->health))
    P_SetPspritePtr(psp, psp->state->args[0]);
}

//
// A_GunFlashTo
// Sets the weapon flash layer to the specified state.
//   args[0]: State number
//   args[1]: If nonzero, don't change the player actor state
//
void A_GunFlashTo(mobj_t *actor, player_t *player, pspdef_t *psp)
{
  if (!psp->state)
    return;

  if(!psp->state->args[1])
    P_SetMobjState(player->mo, S_PLAY_ATK2);

  P_SetPsprite(ps_flash, psp->state->args[0]);
}
