// Preparation-only adapter. The production file is unchanged; this TU replaces
// its normal object exclusively in the isolated preparation executable.
#include "engine/render/town_scene.cpp"
namespace devilution {
void SetPreparationArchitecture(std::vector<TownSceneModel> models)
{
    Scene=std::move(models);
    ++SceneRevision;
}
}
