extends RefCounted
## Read only: geometry, UV and embedded RGB are decoded directly from D3DMESH1.

static func read_model(path: String, origin: Array = [0, 0, 0]) -> Dictionary:
	var bytes := FileAccess.get_file_as_bytes(path)
	if bytes.size() < 20 or bytes.slice(0, 8).get_string_from_ascii() != "D3DMESH1":
		return {"ok": false, "error": "Arquivo D3D incompleto ou formato desconhecido: " + path.get_file()}
	var count := bytes.decode_u32(8)
	var width := bytes.decode_u32(12)
	var height := bytes.decode_u32(16)
	if count < 1 or count > 20000 or width < 1 or height < 1 or width > 2048 or height > 2048:
		return {"ok": false, "error": "Cabeçalho D3D fora dos limites da engine."}
	if bytes.size() != 20 + count * 60 + width * height * 3:
		return {"ok": false, "error": "O tamanho do D3D diverge de seu cabeçalho."}
	var offset := Vector3(float(origin[0]), float(origin[1]), float(origin[2])) if origin.size() == 3 else Vector3(float(origin[0]), 0, float(origin[1]))
	var vertices := PackedVector3Array()
	var uv := PackedVector2Array()
	vertices.resize(count * 3)
	uv.resize(count * 3)
	for triangle in count:
		# Godot front faces use clockwise winding; flip only vertex order, never positions.
		for local_index in 3:
			var original_index: int = [0, 2, 1][local_index]
			var source_index := triangle * 3 + original_index
			var vertex := Vector3(bytes.decode_float(20 + source_index * 20), bytes.decode_float(24 + source_index * 20), bytes.decode_float(28 + source_index * 20))
			var texcoord := Vector2(bytes.decode_float(32 + source_index * 20), bytes.decode_float(36 + source_index * 20))
			if not vertex.is_finite() or not texcoord.is_finite():
				return {"ok": false, "error": "D3D contém coordenadas não finitas."}
			vertices[triangle * 3 + local_index] = vertex + offset
			uv[triangle * 3 + local_index] = texcoord
	var image := Image.create_from_data(width, height, false, Image.FORMAT_RGB8, bytes.slice(20 + count * 60))
	var material := StandardMaterial3D.new()
	material.albedo_texture = ImageTexture.create_from_image(image)
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	var builder := SurfaceTool.new()
	builder.begin(Mesh.PRIMITIVE_TRIANGLES)
	builder.set_smooth_group(-1)
	for index in vertices.size():
		builder.set_uv(uv[index])
		builder.add_vertex(vertices[index])
	builder.generate_normals()
	var instance := MeshInstance3D.new()
	instance.mesh = builder.commit()
	instance.material_override = material
	instance.set_meta("review_original_materials", [material])
	return {"ok": true, "node": instance, "triangles": count, "texture": [width, height], "sha256": FileAccess.get_sha256(path), "origin": offset}
