extends SceneTree
## Regressions for the cathedral flattening: fit must preserve source proportions.
const Source = preload("res://addons/tristram_editor/glb_source.gd")
var checks := 0
var failures := 0

func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		failures += 1
		push_error(message)

func _initialize() -> void:
	var node := Node3D.new()
	var fit := {"rotatedSourceMin": [-1, -2, -3], "rotatedSourceMax": [1, 2, 3], "targetMinRelative": [-2, 0, -4], "targetMaxRelative": [18, 6, 8], "yawDegrees": 0}
	check(Source.apply_recorded_fit(node, fit), "Valid proxy fit is accepted")
	check(node.basis.get_scale().is_equal_approx(Vector3(2, 2, 2)), "Target height must not flatten the source")
	check((node.transform * Vector3(-1, -2, -3)).is_equal_approx(Vector3(6, 0, -4)), "Uniform fit centers the footprint and grounds the source")
	check((node.transform * Vector3(1, 2, 3)).is_equal_approx(Vector3(10, 8, 8)), "Source height/width/depth ratios survive")
	check(not fit.has("appliedScale"), "Input receipt is not silently rewritten")
	check(node.get_meta("d3d_fit").appliedScale == [2.0, 2.0, 2.0], "Receipt records actual uniform scale")
	# Rotated AABB with x/z exchanged; rotation and scale must remain separable.
	var rotated := fit.duplicate(true)
	rotated.rotatedSourceMin = [-3, -2, -1]
	rotated.rotatedSourceMax = [3, 2, 1]
	rotated.yawDegrees = 90
	rotated.uniformScale = 3.0
	check(Source.apply_recorded_fit(node, rotated), "Explicit uniform scale and yaw are accepted")
	check(node.basis.get_scale().is_equal_approx(Vector3(3, 3, 3)), "Yaw does not introduce unequal scale")
	check(is_equal_approx((node.basis * Vector3.UP).length(), 3.0), "Vertical lengths remain uniformly scaled")
	var prior := node.transform
	for invalid in [0.0, -1.0, INF, NAN]:
		var bad := fit.duplicate(true)
		bad.uniformScale = invalid
		check(not Source.apply_recorded_fit(node, bad), "Invalid scale is rejected")
		check(node.transform.is_equal_approx(prior), "Rejected fit preserves existing placement")
	var bad_target := fit.duplicate(true)
	bad_target.targetMaxRelative = [-3, 1, 3]
	check(not Source.apply_recorded_fit(node, bad_target), "Inverted target is rejected")
	check(node.transform.is_equal_approx(prior), "Inverted target preserves placement")
	node.free()
	print("MODEL_FIT checks=%d failures=%d" % [checks, failures])
	quit(0 if failures == 0 else 1)
