#define SDL_MAIN_HANDLED
#include "player.h"
#include "engine/render/player_actor_capture.hpp"
#include "data/file.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

namespace devilution {
uint8_t ProgressToNextGameTick = 0;
std::string NativeCaptureTablePath;
// Harness-only fatal handler. No production logging/auth/simulation is changed.
[[noreturn]] void app_fatal(std::string_view) { std::abort(); }
// Fixture-only file transport. Native LoadSpellData, RecordReader and TSV
// parser populate the metadata used by the source-exact native selector.
std::expected<DataFile, DataFile::Error> DataFile::load(std::string_view path)
{
    if (path != "txtdata\\spells\\spelldat.tsv") return std::unexpected(Error::NotFound);
    std::ifstream input(NativeCaptureTablePath, std::ios::binary);
    if (!input) return std::unexpected(Error::OpenFailed);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    auto owned = std::make_unique<char[]>(bytes.size());
    std::memcpy(owned.get(), bytes.data(), bytes.size());
    return DataFile(std::move(owned), bytes.size());
}
void DataFile::reportFatalError(Error, std::string_view) { std::abort(); }
void DataFile::reportFatalFieldError(DataFileField::Error, std::string_view, std::string_view,
    const DataFileField &, std::string_view) { std::abort(); }
}

int main(int argc,char **argv)
{
    using namespace devilution;
    if(argc!=3)return 2;
    NativeCaptureTablePath=argv[2];LoadSpellData();
    if(SpellsData.size()!=static_cast<unsigned>(SpellID::LastDiablo)+1)return 16;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    ActorModel model;std::string error;
    if(!LoadActorModel(bytes,model,error))return 3;
    Player player{};
    player._pClass=HeroClass::Warrior;player._pmode=PM_ATTACK;player._pdir=Direction::SouthWest;
    player._pAFNum=9;player._pSFNum=14;player._pgfxnum=static_cast<uint8_t>(PlayerWeaponGraphic::Sword);
    player.AnimInfo.setNewAnimation({},16,2);
    PlayerActorCaptureContext context;context.levelEpoch=42;context.authoritativeFootpoint={5,0,6};context.retainedInterpolation128=64;
    PlayerActorCapture captured;unsigned checks=0;
    const auto check=[&checks](bool value){if(value)++checks;return value;};
    if(!check(CaptureWarriorActor(player,model,context,captured,error)
            &&captured.snapshot.nativeImpactFrame==8&&captured.nativeWeaponGraphic==2
            &&captured.snapshot.authoritativeFootpoint.x==5))return 4;
    player.AnimInfo.currentFrame=9;
    player.AnimInfo.setNewAnimation({},16,2,AnimationDistributionFlags::RepeatedAction,0,9);
    bool sawPrevious=false,sawNew=false;
    for(unsigned tick=0;tick<18;++tick){
        ProgressToNextGameTick=context.retainedInterpolation128;
        const int nativeFrame=player.AnimInfo.getFrameToUseForRendering();
        const auto beforeFrame=player.AnimInfo.currentFrame;
        const auto beforeCounter=player.AnimInfo.tickCounterOfCurrentFrame;
        if(!CaptureWarriorActor(player,model,context,captured,error)||captured.renderPhase.frame!=nativeFrame
            ||beforeFrame!=player.AnimInfo.currentFrame||beforeCounter!=player.AnimInfo.tickCounterOfCurrentFrame)return 5;
        sawPrevious|=captured.renderPhase.previousRecovery;
        sawNew|=!captured.renderPhase.previousRecovery;
        player.AnimInfo.processAnimation();
    }
    if(!check(sawPrevious&&sawNew))return 6;
    context.paused=true;
    if(!CaptureWarriorActor(player,model,context,captured,error))return 7;
    const auto held=captured.renderPhase;
    ProgressToNextGameTick=127;
    if(!check(CaptureWarriorActor(player,model,context,captured,error)
            &&captured.renderPhase.frame==held.frame&&captured.renderPhase.subframe==held.subframe))return 8;
    player._pmode=PM_SPELL;
    player.executedSpell={SpellID::Firebolt,SpellType::Spell,0,1};
    player.queuedSpell={SpellID::Lightning,SpellType::Scroll,7,1};
    player._pRSpell=SpellID::Healing;
    if(!check(CaptureWarriorActor(player,model,context,captured,error)&&captured.snapshot.nativeEmissionFrame==14
            &&captured.nativeSpellGraphic==ActorPlayerSpellGraphic::Fire))return 9;
    player._pmode=PM_DEATH;player.AnimInfo.currentFrame=15;
    if(!check(CaptureWarriorActor(player,model,context,captured,error)&&captured.snapshot.terminalHold))return 10;
    const auto epoch=captured.levelEpoch;
    context.levelEpoch=0;
    if(!check(!CaptureWarriorActor(player,model,context,captured,error)&&captured.levelEpoch==epoch))return 11;
    context.levelEpoch=42;player._pClass=HeroClass::Rogue;
    if(!check(!CaptureWarriorActor(player,model,context,captured,error)&&captured.levelEpoch==epoch))return 12;
    player._pClass=HeroClass::Warrior;player._pmode=PM_ATTACK;context.paused=false;
    unsigned phaseComparisons=0;
    for(int frames : {16,20})for(int delay : {1,2})for(int skip : {-1,0,2})for(int flag : {0,1,2,4,7}){
        player._pAFNum=frames==20?10:9;
        player.AnimInfo.setNewAnimation({},static_cast<int8_t>(frames),static_cast<int8_t>(delay));
        player.AnimInfo.currentFrame=player._pAFNum;
        player.AnimInfo.setNewAnimation({},static_cast<int8_t>(frames),static_cast<int8_t>(delay),static_cast<AnimationDistributionFlags>(flag),static_cast<int8_t>(skip),player._pAFNum);
        for(int tick=0;tick<frames*delay;++tick){
            for(unsigned fraction : {0U,32U,64U,96U,128U}){
                context.retainedInterpolation128=static_cast<uint8_t>(fraction);ProgressToNextGameTick=context.retainedInterpolation128;
                const int nativeFrame=player.AnimInfo.getFrameToUseForRendering();
                const bool ok=CaptureWarriorActor(player,model,context,captured,error);
                if(nativeFrame<0||nativeFrame>=frames){if(ok)return 13;continue;}
                if(!ok||captured.renderPhase.frame!=nativeFrame){std::fprintf(stderr,"phase mismatch frames%d delay%d skip%d flags%d tick%d fraction%u: %s\n",frames,delay,skip,flag,tick,fraction,error.c_str());return 14;}
                ++phaseComparisons;
            }
            if(player.AnimInfo.isLastFrame())break;
            player.AnimInfo.processAnimation();
        }
    }
    if(!check(phaseComparisons==7700))return 15;
    std::printf("{\"checks\":%u,\"nativeRepeatedTicksCompared\":18,\"nativePhaseComparisons\":%u,\"actualAnimationInfoUsed\":true,\"nativeTableParserUsed\":true,\"sourceExactGraphicSelectorUsed\":true,\"captureMutatedPlayer\":false,\"worldRendererConnected\":false}\n",checks,phaseComparisons);
    return 0;
}
