#pragma once

// PRIVATE, SOURCE-ONLY companion. Include after town_follow_input_checks.hpp in
// town_first_person_runtime_smoke.cpp; call ItemLabelFppChecks(api, output) once
// immediately after FollowClickChecks(api). Reuses that process's ONLY native
// initialization, loopback, game handler and installed physical-service seam.
#include "item_label_runtime_checks.hpp"

namespace {

std::string ItemLabelFppMapWitness()
{
    std::string result;
    const auto append = [&](const auto &data) {
        result.append(reinterpret_cast<const char *>(&data), sizeof(data));
    };
    append(dPiece);
    append(SOLData);
    append(dSpecial);
    append(dMonster);
    append(dPlayer);
    append(dFlags);
    return result;
}

void ItemLabelFppChecks(const ProductionAccess &api, const std::filesystem::path &output)
{
    using namespace item_label_runtime_checks;
    Check(FixtureHandlerInstalled && FixtureServicesInstalled && IsLoopback
        && gbRunGame && gbActive && HeadlessMode && renderer == nullptr,
        "FPP labels reuse the initialized native loopback, live handler and physical-service seam");
    Check(CurrentEventHandler == GetGameEventHandlerForDiagnostics(),
        "FPP label companion starts with the actual production game handler");
    Check(ActiveItemCount == 0, "FPP label companion stages one item in the existing empty native item pool");

    // All relocation/staging precedes the witness. No InitItems, generator,
    // reseed, second initialization, simulation tick or packet fabrication.
    api.release();
    DrainEmittedWalks(Check); // release-owned prior-suite commands, BEFORE witness
    Check(PhysicalServices::HeldPhysicalKeys.empty(), "prior input suite leaves no physical movement key held");
    ClosePanels();
    ControlDevice = ControlMode = ControlTypes::KeyboardAndMouse;
    PauseMode = 0;
    pcurs = CURSOR_HAND;
    Check(MyPlayer != nullptr && MyPlayer->HoldItem.isEmpty() && !MyPlayerIsDead,
        "FPP label setup retains the healthy native player and empty hand");
    FixturePlacePlayer(FindClearPatch(Check));
    const Point origin = MyPlayer->position.tile;
    const Point itemTile { origin.x - 2, origin.y - 2 };
    Check(InDungeonBounds(itemTile) && IsTileNotSolid(itemTile)
        && dItem[itemTile.x][itemTile.y] == 0 && dPlayer[itemTile.x][itemTile.y] == 0
        && dMonster[itemTile.x][itemTile.y] == 0,
        "FPP Cape occupies a real empty SOL cell in the existing clear patch");
    const int slot = ActiveItems[0];
    Check(slot >= 0 && slot < MAXITEMS && Items[slot].isEmpty(), "native free-list slot is valid and empty before staging");
    const Item previousItem = Items[slot];
    const auto previousCell = dItem[itemTile.x][itemTile.y];
    UiRestore uiRestore;
    InitItemGFX(); // retail CLX loading, not item generation or RNG
    const auto cape = std::find_if(AllItemsList.begin(), AllItemsList.end(),
        [](const ItemData &entry) { return entry.iCurs == ICURS_CAPE; });
    Check(cape != AllItemsList.end(), "retail item table supplies the original Cape for FPP labels");
    Item item;
    InitializeItem(item, static_cast<_item_indexes>(cape - AllItemsList.begin()));
    const int id = PlaceItemInWorld(std::move(item),
        { static_cast<uint8_t>(itemTile.x), static_cast<uint8_t>(itemTile.y) });
    Items[id].setNewAnimation(false);
    Check(id == slot && ActiveItemCount == 1 && Items[id].AnimInfo.sprites.has_value()
        && Items[id].AnimInfo.isLastFrame(), "FPP staged Cape has its real completed retail drop sprite");

    GetOptions().Gameplay.showItemLabels.SetValue(true);
    HighlightKeyPressed(false);
    GetOptions().Graphics.zoom.SetValue(false);
    GetOptions().Graphics.townViewGpuRendering.SetValue(false);
    GetOptions().Graphics.townViewAntialiasing.SetValue(false);
    if (!IsTownViewActive()) ToggleTownView();
    SetTownViewCameraMode(TownCameraMode::FirstPerson);
    SetTownViewCameraPoseForDiagnostics({ Pi / 4, 0.55F, 0, {} });
    api.release(); // Mode selection may request capture; begin with an explicit native release.
    api.sync();
    Check(!api.captured(), "mode/pose preparation cannot reacquire capture without a fresh gesture");
    Check(gnScreenWidth > 0 && gnScreenHeight > 0 && gnViewportHeight > 0
        && gnViewportHeight <= gnScreenHeight, "FPP labels use the current actual logical viewport");
    OwnedSurface storage(gnScreenWidth, gnScreenHeight);
    const Surface out = storage;

    const auto mapBefore = ItemLabelFppMapWitness();
    const auto actorBefore = Sample();
    const auto itemBefore = ItemWitness(); // includes RNG + item-grid/list/animation
    const Point viewBefore = ViewPosition;
    const auto cameraBefore = GetTownViewCameraState();
    const size_t checksBefore = Checks;
    size_t frames = 0, captureGestures = 0;
    const auto witness = [&] {
        Check(ItemLabelFppMapWitness() == mapBefore && Sample() == actorBefore
            && ItemWitness() == itemBefore && ViewPosition == viewBefore,
            "FPP label draw/capture/release preserve native map, SOL, actor, item and RNG state");
        const auto camera = GetTownViewCameraState();
        Check(camera.mode == cameraBefore.mode && camera.yaw == cameraBefore.yaw
            && camera.pitch == cameraBefore.pitch && camera.distance == cameraBefore.distance
            && camera.offsetX == cameraBefore.offsetX && camera.offsetZ == cameraBefore.offsetZ,
            "label capture gestures do not manufacture a different camera pose");
    };
    const auto publish = [&] {
        Check(++frames <= 3, "FPP label companion bounds its actual native world publications to three");
        Check(DrawWorldViewForDiagnostics(out, ViewPosition), "FPP labels follow actual DrawGame/DrawTownView publication");
        const auto state = GetTownViewRendererState();
        Check(IsTownViewActive() && GetTownViewCameraMode() == TownCameraMode::FirstPerson
            && GetTownViewFollowCameraState().valid && !state.requestedGpu && !state.usedGpu
            && TownViewItemLabelFrameId() != 0,
            "FPP caption control is a valid published CPU frame with current label epoch");
        const auto anchors = GetTownViewItemLabelAnchors();
        const auto anchor = std::find_if(anchors.begin(), anchors.end(),
            [id](const auto &entry) { return entry.itemIndex == id; });
        Check(anchor != anchors.end() && anchor->tile == itemTile
            && anchor->position.x >= 80 && anchor->position.x < out.w() - 80
            && anchor->position.y >= 32 && anchor->position.y < gnViewportHeight - 32,
            "current FPP Cape anchor lies safely inside the real world viewport");
        BeginUiOverlayRegions(out);
        witness();
    };
    const auto setCapture = [&](bool capture) {
        // Suspend first even for acquisition: every acquisition is a new SDL
        // DOWN/UP through MakeProductionAccess, not a boolean policy override.
        api.release();
        api.sync();
        Check(!api.captured() && !PhysicalServices::Relative,
            "native Suspend/Sync actually release relative capture before the next gesture");
        if (capture) {
            ++captureGestures;
            api.deliberateResume();
            api.sync();
            Check(api.captured() && PhysicalServices::Relative,
                "fresh SDL DOWN/UP acquires actual native FPP policy through the existing handler");
        }
        Check(first_person_root_supplement::TakeWalksWithoutParse(Check).empty(),
            "release/resume gesture emits no loopback gameplay command and needs no parser cleanup");
        Check(sgbMouseDown == CLICK_NONE && LastPlayerAction == PlayerActionType::None,
            "fresh capture DOWN has its real matching UP and leaves no native held action");
        witness();
    };
    RunCapturedPointerPolicyChecks(out, id, publish, setCapture, Check);
    Check(frames == 3 && captureGestures == 1 && !api.captured(),
        "FPP caption selection passed released/captured/released with one real acquisition gesture");
    witness();
    std::ofstream receipt(output / "first-person-item-label-checks.json", std::ios::binary);
    receipt << "{\"format\":\"d3d.private-fpp-item-label-native-checks-r1\",\"status\":\"PASS\",\"completed\":true,\"checks\":"
            << (Checks - checksBefore) << ",\"worldFrames\":" << frames
            << ",\"captureGestures\":" << captureGestures
            << ",\"actualWorldAndLabels\":true,\"actualProductionHandler\":true,\"actualLoopback\":true,\"physicalServicesSimulated\":true,\"nativeWitnessExact\":true"
            << ",\"physicalCaptureTested\":false,\"gpuExecuted\":false,\"fullHudPresentTested\":false,\"itemPickupTested\":false,\"gameplayClaim\":false,\"fpsClaim\":false,\"installed\":false}\n";
    Check(receipt.good(), "write FPP caption receipt only after all actual observed checks returned");
    // Successful-case teardown only. Failures leave the isolated process to
    // the existing runner's Cleanup/abort; no human profile was loaded/written.
    api.release(); api.sync();
    dItem[itemTile.x][itemTile.y] = previousCell;
    ActiveItemCount = 0;
    Items[slot] = previousItem;
    SetTownViewCameraMode(TownCameraMode::ThirdPerson); // invalidate staged-item anchors
    ResetItemlabelHighlighted(); ClearUiOverlayRegions();
}

} // namespace
