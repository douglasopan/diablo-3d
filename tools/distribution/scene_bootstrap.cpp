#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "engine/assets.hpp"
#include "engine/load_file.hpp"
#include "engine/load_cel.hpp"
#include "engine/palette.h"
#include "engine/light_tables.hpp"
#include "engine/render/town_scene.hpp"
#include "engine/render/town_visual_scene.hpp"
#include "engine/render/town_visual_objects.hpp"
#include "engine/render/town_scene_logic.hpp"
#include "game_mode.hpp"
#include "headless_mode.hpp"
#include "levels/dun_tile_data.hpp"
#include "levels/town.h"
#include "player.h"
#include "quests.h"
#include "utils/paths.h"
#include "mods/mod_identity.h"
namespace devilution {void SetPreparationArchitecture(std::vector<TownSceneModel> models);}

int main(int argc,char **argv)
{
    const bool validate=argc==6&&std::string_view(argv[5])=="--validate-scene";
    if(argc!=5&&!validate) { std::cerr<<"Usage: prepare-tristram DATA ASSETS PREFABS OUTPUT [--validate-scene]\n";return 2; }
    try {
        using namespace devilution;
        const auto data=std::filesystem::canonical(argv[1]);
        const auto assets=std::filesystem::canonical(argv[2]);
        const auto prefabs=std::filesystem::canonical(argv[3]);
        const auto output=std::filesystem::absolute(argv[4]);
        if(std::filesystem::exists(output))throw std::runtime_error("Output must be a new cache directory");
        if(!std::filesystem::is_regular_file(data/"DIABDAT.MPQ"))throw std::runtime_error("Original DIABDAT.MPQ required");
        SDL_SetMainReady();
        if(SDL_Init(0)!=0)throw std::runtime_error(SDL_GetError());
        paths::SetBasePath(data.string());paths::SetAssetsPath(assets.string());
        paths::SetPrefPath(prefabs.string());paths::SetConfigPath(prefabs.string());
        HeadlessMode=true;gbIsHellfire=gbIsMultiplayer=gbIsSpawn=false;
        currlevel=0;leveltype=DTYPE_TOWN;setlevel=false;
        LoadCoreArchives();
        auto archive=MpqArchive::Open((data/"DIABDAT.MPQ").string().c_str());
        if(!archive)throw std::runtime_error("Cannot open original MPQ");
        MpqArchives.insert_or_assign(MainMpqPriority,std::move(*archive));
        OverridePaths.emplace_back(paths::PrefPath());
        Players.resize(1);MyPlayer=InspectPlayer=&Players[0];MyPlayerId=0;
        MyPlayer->setCharacterLevel(1);MyPlayer->plrlevel=0;MyPlayer->plractive=true;
        for(auto &quest:Quests)quest._qactive=QUEST_NOTAVAIL;
        Quests[Q_PWATER]._qactive=QUEST_INIT;
        if(!LoadLevelSOLData())throw std::runtime_error("Original Town SOL unavailable");
        pDungeonCels=LoadFileInMem("levels\\towndata\\town.cel");
        pMegaTiles=LoadFileInMem<MegaTile>("levels\\towndata\\town.til");
        pSpecialCels=LoadCel("levels\\towndata\\towns",64);
        SetDungeonMicros(pDungeonCels,MicroTileLen);
        CreateTown(ENTRY_MAIN);
        // Freeze the incidental Town entrance variants to the selected snapshot.
        // These are tile identifiers, not artwork or extracted bitmap payloads.
        constexpr int entranceCells[][4]={{36,78,0,0},{36,79,2,0},{37,78,12,0},{37,79,14,0},{38,78,4,0},{38,79,6,0},{39,78,0,0},{39,79,2,0},{40,78,8,0},{40,79,10,0},{41,78,4,0},{41,79,6,0},{42,78,8,0},{42,79,10,0},{43,78,8,0},{43,79,10,0},{44,78,4,0},{44,79,6,0},{45,78,4,0},{45,79,6,0}};
        for(const auto &cell:entranceCells){dPiece[cell[0]][cell[1]]=cell[2];dSpecial[cell[0]][cell[1]]=cell[3];}
        // Palette is loaded directly into memory; no video, audio, window, input,
        // gameplay loop, preferences or saves are created by this preparation.
        std::array<Color,256> pal;
        LoadFileInMem("levels\\towndata\\town.pal",pal);
        for(size_t i=0;i<pal.size();++i)logical_palette[i]=pal[i].toSDL();
        MakeLightTable();
        if(validate){
            const auto &models=GetTownScene();
            const auto &logic=GetTownSceneLogicAudit();
            const auto &fixed=GetTownVisualObjects();
            if(GetTownVisualSceneAudit().status!="matched"||models.size()!=14||!HasTownVisualObjects()||fixed.objects.size()!=25743||fixed.textures.size()!=1349||logic.status!="applied"||logic.cells.size()!=25||logic.routes.size()!=4)
                throw std::runtime_error("Complete selected scene failed native loader/WorldPatch validation");
            std::cout<<"PASS selected scene: 14 architecture objects, 25743 fixed objects, 1349 textures, 25 native cells and 4 routes; no gameplay/window/input\n";
            return 0;
        }
        auto manifest=LoadFileInMemWithStatus(TownVisualSceneManifestPath.data());
        if(!manifest)throw std::runtime_error("Preparation manifest is missing");
        auto ref=FindAsset(TownVisualSceneManifestPath);
        std::vector<std::byte> manifestBytes(ref.size());
        auto manifestHandle=OpenAsset(std::move(ref));
        if(!manifestHandle.read(manifestBytes.data(),manifestBytes.size()))throw std::runtime_error("Cannot read selected manifest");
        std::vector<TownSceneModel> models;
        auto resolver=[](std::string_view path,std::vector<std::byte> &bytes,std::string &kind,std::string &failure){
            auto resource=FindAsset(path);if(!resource.ok()){failure="missing prefab";return false;}
            bytes.resize(resource.size());kind="override";
            auto handle=OpenAsset(std::move(resource));return handle.read(bytes.data(),bytes.size());
        };
        // Only architecture is parsed here. Static materials are generated next
        // and cannot be admitted into the game until their sealed hashes pass.
        if(!ParseTownVisualScene(models,manifestBytes,resolver,[](auto,auto,std::string &){return true;})||models.size()!=14)
            throw std::runtime_error("Selected architecture prefabs were not admitted");
        SetPreparationArchitecture(std::move(models));
        std::string failure;
        if(!ExportTownViewFixedObjects(output,failure))throw std::runtime_error(failure);
        const auto file=output/"tristram.d3dvis";
        std::vector<std::byte> generated(std::filesystem::file_size(file));
        std::ifstream stream(file,std::ios::binary);stream.read(reinterpret_cast<char*>(generated.data()),generated.size());
        if(!stream||ModHashToHex(ComputeBytesSha256(generated))!="d463e0dee0f022f7b5ecbf9912a787215fa8608fcab7077fbf8cf2fc591198ff")
            throw std::runtime_error("Original data is incompatible with the selected Tristram baseline; installation refused");
        std::cout<<"Prepared original Town materials in an isolated local cache\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<"\n";return 1;}
}
