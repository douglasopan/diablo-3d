#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "engine/render/town_camera_collision.hpp"

using namespace devilution;
size_t Checks = 0;
void Check(bool value, const char *name)
{
	++Checks;
	if (!value) throw std::runtime_error(name);
	std::cout << "PASS " << name << '\n';
}
bool Close(float a, float b, float epsilon = 0.0001F) { return std::abs(a - b) < epsilon; }
void Wall(std::vector<TownCameraCollisionTriangle> &triangles, float x, float lowY, float highY, float lowZ, float highZ)
{
	const TownCameraPoint a { x, lowY, lowZ }, b { x, highY, lowZ }, c { x, highY, highZ }, d { x, lowY, highZ };
	triangles.push_back({ { a, b, c } }); triangles.push_back({ { a, c, d } });
}
int main()
{
	try {
		TownCameraCollisionIndex index;
		Check(index.Sweep({0,0,0},{10,0,0},.5F).fraction == 1, "empty scene preserves the desired eye");
		std::vector<TownCameraCollisionTriangle> triangles;
		Wall(triangles,5,-2,2,-2,2); index.Build(triangles);
		Check(index.triangleCount() == 2 && index.bytes() > 0, "finite wall builds a nonempty cached BVH");
		const auto forward = index.Sweep({0,0,0},{10,0,0},.5F);
		Check(forward.valid && !forward.initialOverlap && Close(forward.fraction,.45F), "sphere contacts the front face before its center crosses");
		Check(Close(index.Sweep({10,0,0},{0,0,0},.5F).fraction,.45F), "back faces are equally solid");
		Check(index.Sweep({0,3,0},{10,3,0},.5F).fraction == 1, "empty space above a wall remains open");
		Check(Close(index.Sweep({0,2.3F,0},{10,2.3F,0},.5F).fraction,.46F), "swept sphere catches a triangle edge missed by an eye ray");
		Check(Close(index.Sweep({0,2.3F,2.4F},{10,2.3F,2.4F},.6F).fraction,
		    (5-std::sqrt(.36F-.09F-.16F))/10), "swept sphere catches a triangle corner");
		Check(index.Sweep({4.5F,0,0},{0,0,0},.5F).fraction == 1, "starting tangent and moving away does not stick to the wall");
		Check(index.Sweep({4.5F,0,0},{10,0,0},.5F).fraction == 0, "starting tangent and moving inward is blocked immediately");
		const auto overlap = index.Sweep({4.8F,0,0},{0,0,0},.5F);
		Check(overlap.initialOverlap && overlap.fraction == 0, "initial penetration is explicit and never ignored as a zero hit");
		const auto separated = index.Separate({4.8F,0,0},{0,0,0},.5F);
		Check(separated.resolved && separated.point.x < 4.5F && !index.Sweep(separated.point,separated.point,.5F).initialOverlap,
		    "initial near-plane overlap separates to the existing side");
		const auto onPlane = index.Separate({5,0,0},{0,0,0},.5F);
		Check(onPlane.resolved && onPlane.point.x < 4.5F, "exactly-on-plane ambiguity uses the previous safe side");
		Check(index.Sweep({0,0,0},{10000,0,0},.5F).fraction > 0 && index.Sweep({0,0,0},{10000,0,0},.5F).fraction < .001F,
		    "fast motion cannot tunnel through a thin wall");
		Check(index.Sweep({0,0,0},{0,0,0},.5F).fraction == 1, "zero-length safe query is valid");
		const float nan = std::numeric_limits<float>::quiet_NaN();
		Check(!index.Sweep({nan,0,0},{0,0,0},.5F).valid && !index.Sweep({0,0,0},{0,0,0},nan).valid,
		    "nonfinite eye and radius fail explicitly");
		Check(!index.Sweep({0,0,0},{0,0,0},0).valid, "invalid radius never disables collision silently");
		triangles.clear();
		Wall(triangles,5,-2,2,-3,-1); Wall(triangles,5,-2,2,1,3);
		Wall(triangles,5,1,3,-1,1); Wall(triangles,5,-3,-1,-1,1); index.Build(triangles);
		Check(index.Sweep({0,0,0},{10,0,0},.5F).fraction == 1, "a real window opening stays empty instead of colliding with its AABB");
		Wall(triangles,5,-1,1,-.08F,.08F); index.Build(triangles);
		Check(index.Sweep({0,0,0},{10,0,0},.5F).fraction < 1, "a real thin window bar blocks the near-plane sphere");
		triangles.clear(); Wall(triangles,5,-10,10,-10,10);
		for (auto &triangle : triangles) for (auto &point : triangle.vertices) point.x += point.z * .5F;
		index.Build(triangles);
		Check(Close(index.Sweep({0,0,0},{10,0,0},.5F).fraction,(5-.5F*std::sqrt(1.25F))/10),
		    "an oblique wall is swept using its actual plane");
		triangles.clear(); Wall(triangles,0,-10,10,-10,10);
		for (auto &triangle : triangles) for (auto &point : triangle.vertices) std::swap(point.x,point.height);
		index.Build(triangles);
		Check(Close(index.Sweep({0,2,0},{0,-2,0},.5F).fraction,.375F), "horizontal roof or floor blocks vertical motion");
		triangles.clear();
		for (int i=0;i<10000;++i) Wall(triangles,static_cast<float>(i*4),-2,2,-2,2);
		index.Build(triangles);
		const auto local = index.Sweep({1,0,0},{3,0,0},.2F);
		Check(local.fraction == 1 && local.trianglesTested < 32 && local.nodesVisited < 80,
		    "BVH query visits local leaves, not all 20000 triangles");
		TownCameraCollisionTriangle invalid {};
		invalid.vertices[0].x = nan; triangles.push_back(invalid);
		triangles.push_back({}); index.Build(triangles);
		Check(index.skippedTriangles()==2 && index.triangleCount()==20000, "invalid or zero-area geometry is counted and omitted");
		triangles.clear(); Wall(triangles,100000,-2,2,-2,2); index.Build(triangles);
		const auto rounded = index.Separate({100000,0,0},{0,0,0},.501F);
		Check(!rounded.resolved || !index.Sweep(rounded.point,rounded.point,.501F).initialOverlap,
		    "separation never publishes a rounded float point still penetrating a wall");
		triangles.clear(); Wall(triangles,0,-2,2,-2,2);
		const size_t cornerBegin = triangles.size(); Wall(triangles,0,-2,2,-2,2);
		for (size_t i=cornerBegin;i<triangles.size();++i) for (auto &point:triangles[i].vertices) std::swap(point.x,point.z);
		index.Build(triangles);
		const auto corner = index.Separate({.1F,0,.1F},{2,0,2},.5F);
		Check(corner.resolved && corner.point.x >= .5F && corner.point.z >= .5F,
		    "bounded separation resolves two perpendicular walls without crossing either");
		Check(index.Sweep({2,0,2},{-2,0,-2},.5F).fraction < .5F,
		    "temporal sweep blocks a desired eye on the opposite side of a corner");
		TownCameraRig rig; rig.SetMode(TownCameraMode::FirstPerson);
		const auto frame = BuildTownCameraFrame(rig,{0,0,0},1920,1080,960,540);
		const float radius = TownCameraCollisionRadius(frame);
		Check(radius > frame.nearClip, "near-plane radius includes both eye offset and corners");
		const auto wide = BuildTownCameraFrame(rig,{0,0,0},3840,1080,1920,540);
		Check(TownCameraCollisionRadius(wide) > radius, "wide aspect expands near-plane collision clearance");
		Check(TownCameraCollisionRadius({}) == 0, "invalid frame has no invented collision radius");
		index.Clear(); Check(index.triangleCount()==0 && index.Sweep({0,0,0},{10,0,0},.5F).fraction==1,
		    "scene reset discards stale blockers");
		std::cout << "COMPLETE collision checks=" << Checks << " failures=0\n";
		return 0;
	} catch (const std::exception &error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
