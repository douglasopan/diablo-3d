/**
 * @file track.cpp
 *
 * Implementation of functionality tracking what the mouse cursor is pointing at.
 */
#include "track.h"
#include "diablo.h"

#include "controls/control_mode.hpp"
#include "controls/game_controls.h"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "engine/point.hpp"
#include "player.h"
#include "stores.h"

namespace devilution {

namespace {

void RepeatWalk(Player &player)
{
	if (!InDungeonBounds(cursPosition))
		return;

	if (player._pmode != PM_STAND && (!player.isWalking() || player.AnimInfo.getFrameToUseForRendering() <= 6))
		return;

	const Point target = player.GetTargetPosition();
	if (cursPosition == target)
		return;

	NetSendCmdLoc(MyPlayerId, true, CMD_WALKXY, cursPosition);
}

} // namespace

void InvalidateTargets()
{
	if (pcursmonst != -1) {
		const Monster &monster = Monsters[pcursmonst];
		if (monster.isInvalid || monster.hasNoLife()
		    || (monster.flags & MFLAG_HIDDEN) != 0
		    || !IsTileLit(monster.position.tile)) {
			pcursmonst = -1;
		}
	}

	if (ObjectUnderCursor != nullptr && !ObjectUnderCursor->canInteractWith())
		ObjectUnderCursor = nullptr;

	if (PlayerUnderCursor != nullptr) {
		const Player &targetPlayer = *PlayerUnderCursor;
		if (targetPlayer._pmode == PM_DEATH || targetPlayer._pmode == PM_QUIT || !targetPlayer.plractive
		    || !targetPlayer.isOnActiveLevel() || targetPlayer.hasNoLife()
		    || !IsTileLit(targetPlayer.position.tile))
			PlayerUnderCursor = nullptr;
	}
}

void RepeatPlayerAction()
{
	if (IsDungeon3DActionContextForRepeat()) {
		if (!CanRepeatDungeon3DWorldAction() || (sgbMouseDown != CLICK_LEFT && sgbMouseDown != CLICK_RIGHT)) {
			LastPlayerAction=PlayerActionType::None;
			return;
		}
	}
	if (pcurs != CURSOR_HAND)
		return;

	if (sgbMouseDown == CLICK_NONE && ControllerActionHeld == GameActionType_NONE)
		return;

	if (IsPlayerInStore())
		return;

	if (LastPlayerAction == PlayerActionType::None && !IsDungeon3DActionContextForRepeat())
		return;

	Player &myPlayer = *MyPlayer;
	if (myPlayer.destAction != ACTION_NONE)
		return;
	if (myPlayer._pInvincible)
		return;
	if (!myPlayer.CanChangeAction())
		return;
	if (IsDungeon3DActionContextForRepeat()) {
		// A hit/death or a temporary invalid aim does not end the held episode.
		// Re-resolve current aim only when the native player can begin an action.
		if (sgbMouseDown == CLICK_LEFT && !RefreshDungeon3DStandingAttackTarget()) return;
		if (sgbMouseDown == CLICK_RIGHT && !RefreshDungeon3DStandingSpellTarget()) return;
	}

	const bool rangedAttack = myPlayer.UsesRangedWeapon();
	switch (LastPlayerAction) {
	case PlayerActionType::Attack:
		if (InDungeonBounds(cursPosition))
			NetSendCmdLoc(MyPlayerId, true, rangedAttack ? CMD_RATTACKXY : CMD_SATTACKXY, cursPosition);
		break;
	case PlayerActionType::AttackMonsterTarget:
		if (pcursmonst != -1)
			NetSendCmdParam1(true, rangedAttack ? CMD_RATTACKID : CMD_ATTACKID, pcursmonst);
		break;
	case PlayerActionType::AttackPlayerTarget:
		if (PlayerUnderCursor != nullptr && !myPlayer.friendlyMode)
			NetSendCmdParam1(true, rangedAttack ? CMD_RATTACKPID : CMD_ATTACKPID, PlayerUnderCursor->getId());
		break;
	case PlayerActionType::Spell:
		if (ControlMode != ControlTypes::KeyboardAndMouse) {
			UpdateSpellTarget(MyPlayer->_pRSpell);
		}
		CheckPlrSpell(ControlMode == ControlTypes::KeyboardAndMouse);
		break;
	case PlayerActionType::SpellMonsterTarget:
		if (pcursmonst != -1)
			CheckPlrSpell(false);
		break;
	case PlayerActionType::SpellPlayerTarget:
		if (PlayerUnderCursor != nullptr && !myPlayer.friendlyMode)
			CheckPlrSpell(false);
		break;
	case PlayerActionType::OperateObject:
		if (ObjectUnderCursor != nullptr && !ObjectUnderCursor->isDoor()) {
			NetSendCmdLoc(MyPlayerId, true, CMD_OPOBJXY, cursPosition);
		}
		break;
	case PlayerActionType::Walk:
		RepeatWalk(myPlayer);
		break;
	case PlayerActionType::None:
		break;
	}
}

bool track_isscrolling()
{
	return LastPlayerAction == PlayerActionType::Walk;
}

} // namespace devilution
