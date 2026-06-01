#include "stdsfx.h"
#include "Object3D.h"
#include <Importer/StepLoader.h>
#include <Renderer/RenderCommand.h>

#include <functional>
#include <fstream>
#include <cctype>
#include <algorithm>
#include <limits>

#include "Application.h"

namespace
{
BoundingSphere TransformBoundingSphere(const BoundingSphere& sphere, const glm::mat4& transform)
{
	if (!sphere.Valid)
		return {};

	BoundingSphere transformed = sphere;
	transformed.Center = glm::vec3(transform * glm::vec4(sphere.Center, 1.0f));
	const float maximumScale = std::max({
		glm::length(glm::vec3(transform[0])),
		glm::length(glm::vec3(transform[1])),
		glm::length(glm::vec3(transform[2]))
	});
	transformed.Radius *= maximumScale;
	return transformed;
}

void MergeBoundingSphere(BoundingSphere& aggregate, const BoundingSphere& sphere)
{
	if (!sphere.Valid)
		return;
	if (!aggregate.Valid)
	{
		aggregate = sphere;
		return;
	}

	const glm::vec3 offset = sphere.Center - aggregate.Center;
	const float distance = glm::length(offset);
	if (distance + sphere.Radius <= aggregate.Radius)
		return;
	if (distance + aggregate.Radius <= sphere.Radius)
	{
		aggregate = sphere;
		return;
	}

	const float radius = (distance + aggregate.Radius + sphere.Radius) * 0.5f;
	if (distance > 0.000001f)
		aggregate.Center += offset * ((radius - aggregate.Radius) / distance);
	aggregate.Radius = radius;
}
}

Mesh::~Mesh()
{
	GeometryLibrary::Release(VertexObject);
	GeometryLibrary::Release(EdgeVertexObject);
}

void Mesh::UpdateBoundingSphere()
{
	Bounds = {};
	if (TraceVertices.empty())
		return;

	glm::vec3 minimum(std::numeric_limits<float>::max());
	glm::vec3 maximum(std::numeric_limits<float>::lowest());
	for (const auto& vertex : TraceVertices)
	{
		minimum = glm::min(minimum, vertex.Position);
		maximum = glm::max(maximum, vertex.Position);
	}

	Bounds.Center = (minimum + maximum) * 0.5f;
	for (const auto& vertex : TraceVertices)
		Bounds.Radius = std::max(Bounds.Radius, glm::length(vertex.Position - Bounds.Center));
	Bounds.Valid = true;
}


void Mesh::Draw(const glm::mat4& view, const glm::mat4 proj, const glm::mat4& parentTransform,
	float opacity, bool transparentPass)
{
	const glm::mat4 model = parentTransform * Transfm.GetMatrix();
	Mat->Bind();
	Ref<Shader> shader = Mat->GetShader();
	if (!shader)
		return;
	Ref<VertexArray> vertexObject = GeometryLibrary::Resolve(VertexObject);
	if (!vertexObject)
		return;
	shader->SetMat4("u_View", view);
	shader->SetMat4("u_Projection", proj);
	shader->SetMat4("u_Model", model);
	shader->SetFloat("u_ObjectOpacity", glm::clamp(opacity, 0.0f, 1.0f));
	shader->SetInt("u_TransparentPass", transparentPass ? 1 : 0);
	{
		//phong stuff
		glm::vec3 viewpos = glm::vec3(glm::inverse(view)[3]);
		shader->SetFloat3("u_lightPos", glm::vec3(0,10,0));
		shader->SetFloat3("u_viewPos", viewpos);
		shader->SetFloat3("u_lightColor", glm::vec3(0, 1, 0));
		shader->SetFloat3("u_objectColor", glm::vec3(1, 1, 1));

		// CSM shadow uniforms (set when shader supports them)
		Application& app = Application::Get();
		CSM& csm = app.GetCSM();
		auto& lightViewProj = csm.GetLightViewProjMatrices();
		auto& cascadeDists = csm.GetCascadeDistances();
		uint32_t cascadeCount = csm.Enabled() ? csm.GetCascadeCount() : 0;
		auto lightDir = csm.GetLight().Direction;

		shader->SetFloat3("u_lightPos", -glm::normalize(lightDir) * 1000.0f);
		shader->SetFloat3("u_lightColor", csm.GetLight().Color * csm.GetLight().Intensity);
		shader->SetInt("u_cascadeCount", (int)cascadeCount);
		shader->SetFloat("u_shadowMapSize", (float)csm.GetShadowMapSize());
		shader->SetFloat("u_shadowConstantBias", csm.ConstantBias());
		shader->SetFloat("u_shadowSlopeBias", csm.SlopeBias());
		shader->SetFloat3("u_lightDir", lightDir);
		shader->SetInt("u_debugCascadeView", Application::Get().GetDebugCascadeView() ? 1 : 0);
		for (uint32_t i = 0; i <= cascadeCount && i < 4; i++)
		{
			shader->SetFloat("u_cascadeDistances[" + std::to_string(i) + "]", cascadeDists[i]);
			if (i < cascadeCount)
				shader->SetMat4("u_lightViewProj[" + std::to_string(i) + "]", lightViewProj[i]);
		}

		csm.BindShadowTexture(2);
		shader->SetInt("u_shadowMap", 2);
		app.GetProbeGI().Bind(shader);
		app.GetPBRIBL().Bind(shader);
	}
#ifdef G_OPENGL
	Ref<ToonMaterial> toon = std::dynamic_pointer_cast<ToonMaterial>(Mat);
	if (toon && !toon->TwoSided)
	{
		RenderCommand::Enable("CULL_FACE");
		RenderCommand::Cull("Back");
	}
#endif
    RenderCommand::DrawIndexed(vertexObject);
#ifdef G_OPENGL
	if (!transparentPass && toon && toon->EdgeEnabled && toon->Alpha > 0.0f)
	{
		RenderCommand::Enable("CULL_FACE");
		RenderCommand::Cull("Front");
		toon->BindEdge(view, proj, model, Application::Get().GetViewportSize());
		RenderCommand::DrawIndexed(vertexObject);
	}
	if (toon)
		RenderCommand::Disable("CULL_FACE");
#endif
	Ref<VertexArray> edgeVertexObject = GeometryLibrary::Resolve(EdgeVertexObject);
	if (!transparentPass && ShowEdges && edgeVertexObject && EdgeVertexCount > 0)
	{
		auto edgeShader = Application::Get().GetShaderLibrary()->Get("DefaultLineColor");
		edgeShader->Bind();
		edgeShader->SetMat4("u_View", view);
		edgeShader->SetMat4("u_Projection", proj);
		edgeShader->SetMat4("u_Model", model);
		RenderCommand::EnableDepthTest(true);
		RenderCommand::SetLineWidth(2.0f);
		RenderCommand::DrawLines(edgeVertexObject, EdgeVertexCount);
	}
};

void Object3D::Draw(const glm::mat4& view, const glm::mat4 proj, bool transparentPass)
{
	for (auto& mesh : Meshes)
		mesh->Draw(view, proj, Transfm.GetMatrix(), Opacity, transparentPass);
}

void Object3D::UpdateBoundingSphere()
{
	Bounds = {};
	for (const auto& mesh : Meshes)
	{
		if (!mesh)
			continue;
		mesh->UpdateBoundingSphere();
		MergeBoundingSphere(Bounds, TransformBoundingSphere(mesh->Bounds, mesh->Transfm.GetMatrix()));
	}
}

BoundingSphere Object3D::GetWorldBoundingSphere() const
{
	BoundingSphere currentBounds;
	for (const auto& mesh : Meshes)
	{
		if (mesh)
			MergeBoundingSphere(currentBounds, TransformBoundingSphere(mesh->Bounds, mesh->Transfm.GetMatrix()));
	}
	if (!currentBounds.Valid)
		currentBounds = Bounds;
	return TransformBoundingSphere(currentBounds, Transfm.GetMatrix());
}


#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

template<typename T>
bool Object3D::Load(const std::string& filepath) {
	return LoadFromPath<T>(std::filesystem::u8path(filepath));
}

template<typename T>
bool Object3D::LoadFromPath(const std::filesystem::path& filepath) { 

	// Read file into memory using std::filesystem::path (handles encoding on Windows).
	std::ifstream file(filepath, std::ios::binary);
	if (!file)
	{
		std::cerr << "Failed to open file: " << filepath << std::endl;
		return false;
	}
	std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	file.close();
	
	Assimp::Importer importer;
	std::string hint = filepath.extension().string();
	if (!hint.empty() && hint[0] == '.') hint = hint.substr(1);
	std::transform(hint.begin(), hint.end(), hint.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if constexpr (std::is_same_v<T, VertexNormalTexture>)
	{
		if (hint == "step" || hint == "stp")
		{
			StepMeshData stepMesh;
			if (!StepLoader::Load(filepath, stepMesh))
			{
				ERROR("Failed to import STEP model {}: {}", filepath.u8string(), stepMesh.Error);
				return false;
			}

			Meshes.clear();
			Ref<Mesh> mesh = CreateRef<Mesh>();
			Ref<VertexArray> vertexObject = VertexArray::Create();
			auto vbuffer = VertexBuffer::Create(reinterpret_cast<float*>(stepMesh.Vertices.data()),
				stepMesh.Vertices.size() * sizeof(VertexNormalTexture));
			vbuffer->SetLayout({
				BufferElement(ShaderDataType::Float3, "a_Position", false),
				BufferElement(ShaderDataType::Float3, "a_Normal", false),
				BufferElement(ShaderDataType::Float2, "a_TexCoord", false)
			});
			vertexObject->AddVertexBuffer(vbuffer);
			vertexObject->SetIndexBuffer(IndexBuffer::Create(stepMesh.Indices.data(), stepMesh.Indices.size()));
			vertexObject->Unbind();
			mesh->VertexObject = GeometryLibrary::Register(vertexObject);
			mesh->Mat = CreateRef<MaterialPBR>();
			mesh->TraceVertices = std::move(stepMesh.Vertices);
			mesh->TraceIndices = std::move(stepMesh.Indices);
			mesh->EdgeVertices = std::move(stepMesh.EdgeVertices);
			mesh->ShowEdges = true;
			if (!mesh->EdgeVertices.empty())
			{
				std::vector<LineVertex> edgeVertices;
				edgeVertices.reserve(mesh->EdgeVertices.size());
				for (const glm::vec3& position : mesh->EdgeVertices)
					edgeVertices.push_back({ position, glm::vec4(0.02f, 0.02f, 0.02f, 1.0f) });

				Ref<VertexArray> edgeVertexObject = VertexArray::Create();
				auto edgeBuffer = VertexBuffer::Create(reinterpret_cast<float*>(edgeVertices.data()),
					edgeVertices.size() * sizeof(LineVertex));
				edgeBuffer->SetLayout({
					BufferElement(ShaderDataType::Float3, "a_Position", false),
					BufferElement(ShaderDataType::Float4, "a_Color", false)
				});
				edgeVertexObject->AddVertexBuffer(edgeBuffer);
				edgeVertexObject->Unbind();
				mesh->EdgeVertexObject = GeometryLibrary::Register(edgeVertexObject);
				mesh->EdgeVertexCount = static_cast<uint32_t>(edgeVertices.size());
			}
			Meshes.push_back(mesh);
			return true;
		}
	}

	const bool flipPMXTextureUVY = hint == "pmx";
	unsigned int flags = aiProcess_Triangulate | aiProcess_GenNormals;
	if (hint != "gltf" && hint != "glb" && !flipPMXTextureUVY)
	{
		flags |= aiProcess_FlipUVs;
	}
	const aiScene* scene = nullptr;

	// Try ReadFile with the ANSI path first.
	// Then try ReadFileFromMemory from the pre-loaded buffer.
	// For paths with non-ASCII chars, copy to a temp ASCII path first.

	// Check if the path is pure ASCII (no encoding issues with fopen)
	bool isPureAscii = true;
	auto u8str = filepath.u8string();
	for (char c : u8str) {
		if (static_cast<unsigned char>(c) > 127) { isPureAscii = false; break; }
	}

	if (isPureAscii) {
		scene = importer.ReadFile(filepath.string().c_str(), flags);
	} else {
		// Create a temp file with pure ASCII name
		auto tmpDir = std::filesystem::temp_directory_path();
		auto tmpPath = tmpDir / ("kengine_" + hint + ".tmp");
		bool copied = false;
		try {
			std::filesystem::copy_file(filepath, tmpPath, std::filesystem::copy_options::overwrite_existing);
			copied = true;
		} catch (...) {}
		if (copied) {
			scene = importer.ReadFile(tmpPath.string().c_str(), flags);
			std::error_code ec;
			std::filesystem::remove(tmpPath, ec);
		}
	}
	if (!scene) {
		scene = importer.ReadFileFromMemory(buffer.data(), buffer.size(), flags, hint.c_str());
	}
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		std::cerr << "assimp error: " << importer.GetErrorString() << std::endl;
		std::cerr << "assimp Failed to load mesh: " << filepath.u8string() << std::endl;
		return false;
	}

	BufferLayout layout;
	if constexpr (std::is_same_v<T, VertexColor>)
	{
		layout = {
			BufferElement(ShaderDataType::Float3,"a_Position",false),
			BufferElement(ShaderDataType::Float4,"a_Color",false),
		};
	}
	else if constexpr (std::is_same_v<T, VertexNormal>)
	{
		layout = {
			BufferElement(ShaderDataType::Float3,"a_Position",false),
			BufferElement(ShaderDataType::Float3,"a_Normal",false),
		};
	}
	else if constexpr (std::is_same_v<T, VertexNormalTexture>)
	{
		layout = {
			BufferElement(ShaderDataType::Float3,"a_Position",false),
			BufferElement(ShaderDataType::Float3,"a_Normal",false),
			BufferElement(ShaderDataType::Float2,"a_TexCoord",false),
		};
	}

	Meshes.clear();

	auto loadEmbeddedTexture = [&](const aiTexture* texture, const std::string& key) -> TextureHandle
	{
		Ref<Image> image;
		if (texture->mHeight == 0)
		{
			image = Image::LoadFromMemory(reinterpret_cast<const unsigned char*>(texture->pcData), texture->mWidth);
		}
		else
		{
			unsigned char* pixels = new unsigned char[static_cast<size_t>(texture->mWidth) * texture->mHeight * 4];
			for (unsigned int y = 0; y < texture->mHeight; ++y)
			{
				const unsigned int sourceY = texture->mHeight - y - 1;
				for (unsigned int x = 0; x < texture->mWidth; ++x)
				{
					const aiTexel& texel = texture->pcData[sourceY * texture->mWidth + x];
					const size_t target = (static_cast<size_t>(y) * texture->mWidth + x) * 4;
					pixels[target + 0] = texel.r;
					pixels[target + 1] = texel.g;
					pixels[target + 2] = texel.b;
					pixels[target + 3] = texel.a;
				}
			}
			image = CreateRef<Image>(texture->mWidth, texture->mHeight, 4, pixels);
		}
		return TextureLibrary::LoadTexture(key, image);
	};

	auto loadMaterialTexture = [&](aiMaterial* material, std::initializer_list<aiTextureType> textureTypes) -> TextureHandle
	{
		for (aiTextureType textureType : textureTypes)
		{
			aiString textureName;
			if (material->GetTexture(textureType, 0, &textureName) != AI_SUCCESS)
				continue;

			const std::string name = textureName.C_Str();
			const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(name.c_str());
			if (embeddedTexture)
			{
				TextureHandle texture = loadEmbeddedTexture(embeddedTexture, filepath.u8string() + "#" + name);
				if (texture)
					INFO("Loaded embedded model texture: {}", name);
				else
					ERROR("Failed to decode embedded model texture: {}", name);
				return texture;
			}

			std::filesystem::path texturePath = std::filesystem::u8path(name);
			if (texturePath.is_relative())
				texturePath = filepath.parent_path() / texturePath;
			return TextureLibrary::LoadTexture(texturePath.u8string());
		}
		return {};
	};

	auto createMaterial = [&](aiMesh* sourceMesh) -> Ref<Material>
	{
		if constexpr (std::is_same_v<T, VertexColor>)
			return CreateRef<MaterialColor>();

		if constexpr (std::is_same_v<T, VertexNormalTexture>)
		{
			if (hint == "pmx" || hint == "pmd")
			{
				Ref<ToonMaterial> material = CreateRef<ToonMaterial>();
				if (sourceMesh->mMaterialIndex < scene->mNumMaterials)
				{
					aiMaterial* importedMaterial = scene->mMaterials[sourceMesh->mMaterialIndex];
					aiColor3D color;
					float value = 0.0f;
					int twoSided = 0;
					if (importedMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
						material->Diffuse = glm::vec3(color.r, color.g, color.b);
					if (importedMaterial->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS)
						material->Ambient = glm::vec3(color.r, color.g, color.b);
					if (importedMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS)
						material->Specular = glm::vec3(color.r, color.g, color.b);
					if (importedMaterial->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS)
						material->Alpha = value;
					if (importedMaterial->Get(AI_MATKEY_SHININESS_STRENGTH, value) == AI_SUCCESS)
						material->SpecularPower = value;
					if (importedMaterial->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS)
						material->TwoSided = twoSided != 0;
					material->MainTexture = loadMaterialTexture(importedMaterial, { aiTextureType_DIFFUSE });
					material->SphereTexture = loadMaterialTexture(importedMaterial, { aiTextureType_REFLECTION });
					material->ToonTexture = loadMaterialTexture(importedMaterial, { aiTextureType_LIGHTMAP });
					material->SphereMode = material->SphereTexture ? 1 : 0;
				}
				return material;
			}
		}

		Ref<MaterialPBR> material = CreateRef<MaterialPBR>();
		if constexpr (std::is_same_v<T, VertexNormalTexture>)
		{
			if (sourceMesh->mMaterialIndex < scene->mNumMaterials)
			{
				aiMaterial* importedMaterial = scene->mMaterials[sourceMesh->mMaterialIndex];
				aiColor4D baseColor;
				float factor = 0.0f;
				if (importedMaterial->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS ||
					importedMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS)
					material->Albedo = glm::vec3(baseColor.r, baseColor.g, baseColor.b);
				if (importedMaterial->Get(AI_MATKEY_METALLIC_FACTOR, factor) == AI_SUCCESS)
					material->Metallic = factor;
				if (importedMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, factor) == AI_SUCCESS)
					material->Roughness = factor;

				material->AlbedoMap = loadMaterialTexture(importedMaterial,
					{ aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
				material->NormalMap = loadMaterialTexture(importedMaterial,
					{ aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA });
				material->MetallicMap = loadMaterialTexture(importedMaterial, { aiTextureType_METALNESS });
				material->RoughnessMap = loadMaterialTexture(importedMaterial, { aiTextureType_DIFFUSE_ROUGHNESS });
				material->AOMap = loadMaterialTexture(importedMaterial,
					{ aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP });

				if (hint == "gltf" || hint == "glb")
				{
					// glTF metallic-roughness / occlusion textures use B / G / R respectively.
					material->MetallicMapChannel = 2;
					material->RoughnessMapChannel = 1;
					material->AOMapChannel = 0;
				}
			}
		}
		return material;
	};

	auto processMesh = [&](aiMesh* sourceMesh)
	{
		std::vector<T> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(sourceMesh->mNumVertices);

		for (unsigned int i = 0; i < sourceMesh->mNumVertices; ++i)
		{
			T vertex;
			const aiVector3D position = sourceMesh->mVertices[i];
			vertex.Position = glm::vec3(position.x, position.y, position.z);
			if constexpr (std::is_same_v<T, VertexColor>)
			{
				vertex.Color = glm::vec4(1, 0, 0, 1);
			}
			else if constexpr (std::is_same_v<T, VertexNormal> || std::is_same_v<T, VertexNormalTexture>)
			{
				vertex.Normal = glm::vec3(1, 0, 0);
				if (sourceMesh->mNormals)
					vertex.Normal = glm::vec3(sourceMesh->mNormals[i].x, sourceMesh->mNormals[i].y, sourceMesh->mNormals[i].z);
			}
			if constexpr (std::is_same_v<T, VertexTexture> || std::is_same_v<T, VertexNormalTexture>)
			{
				vertex.TexCoord = glm::vec2(0, 0);
				if (sourceMesh->HasTextureCoords(0))
				{
					float texCoordY = sourceMesh->mTextureCoords[0][i].y;
					vertex.TexCoord = glm::vec2(sourceMesh->mTextureCoords[0][i].x, texCoordY);
				}
			}
			vertices.push_back(vertex);
		}

		for (unsigned int i = 0; i < sourceMesh->mNumFaces; ++i)
			for (unsigned int j = 0; j < sourceMesh->mFaces[i].mNumIndices; ++j)
				indices.push_back(sourceMesh->mFaces[i].mIndices[j]);

		Ref<Mesh> mesh = CreateRef<Mesh>();
		Ref<VertexArray> vertexObject = VertexArray::Create();
		auto vbuffer = VertexBuffer::Create(reinterpret_cast<float*>(vertices.data()), vertices.size() * sizeof(T));
		vbuffer->SetLayout(layout);
		vertexObject->AddVertexBuffer(vbuffer);
		vertexObject->SetIndexBuffer(IndexBuffer::Create(indices.data(), indices.size()));
		vertexObject->Unbind();
		mesh->VertexObject = GeometryLibrary::Register(vertexObject);
		mesh->Mat = createMaterial(sourceMesh);
		if constexpr (std::is_same_v<T, VertexNormalTexture>)
		{
			mesh->TraceVertices = vertices;
			mesh->TraceIndices = indices;
		}
		Meshes.push_back(mesh);
	};

	std::function<void(aiNode*)> processNode;
	processNode = [&](aiNode* node)
	{
		for (unsigned int i = 0; i < node->mNumMeshes; ++i)
			processMesh(scene->mMeshes[node->mMeshes[i]]);
		for (unsigned int i = 0; i < node->mNumChildren; ++i)
			processNode(node->mChildren[i]);
	};
	processNode(scene->mRootNode);

	return !Meshes.empty();

}
//template bool Object3D::Load<VertexBase>(const std::string& filepath);
template bool Object3D::Load<VertexColor>(const std::string& filepath);
template bool Object3D::Load<VertexNormal>(const std::string& filepath);
template bool Object3D::LoadFromPath<VertexColor>(const std::filesystem::path& filepath);
template bool Object3D::LoadFromPath<VertexNormal>(const std::filesystem::path& filepath);
template bool Object3D::LoadFromPath<VertexNormalTexture>(const std::filesystem::path& filepath);


//
//template<typename VertexBase>
//bool Object3D::Load<VertexBase>(const std::string& filepath)
//{
//	std::vector<VertexBase> vertices;
//	std::vector<uint32_t> indices;
//	auto processMesh = [&](aiMesh* mesh, const aiScene* scene) {
//			//vector<Texture> textures;
//			for (unsigned int i = 0; i < mesh->mNumVertices; i++)
//			{
//				// process vertex positions, normals and texture coordinates
//				aiVector3D position = mesh->mVertices[i];
//				VertexBase vertex;
//				vertex.Position = glm::vec3(position.x, position.y, position.z);
//				vertices.push_back(vertex);
//			}
//			// process indices
//			uint32_t indices_offset = indices.size();
//			for (uint32_t i = 0; i < mesh->mNumFaces; i++)
//			{
//				aiFace face = mesh->mFaces[i];
//				for (uint32_t j = 0; j < face.mNumIndices; j++)
//					indices.push_back(indices_offset + face.mIndices[j]);
//			}
//		};
//
//	auto processNode = [&](aiNode * node, const aiScene * scene)
//	{
//		// process all the node's meshes (if any)
//		for (unsigned int i = 0; i < node->mNumMeshes; i++)
//		{
//			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
//			meshes.push_back(processMesh(mesh, scene));
//		}
//		// then do the same for each of its children
//		for (unsigned int i = 0; i < node->mNumChildren; i++)
//		{
//			processNode(node->mChildren[i], scene);
//		}
//	};
//
//	Assimp::Importer importer;
//	const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_FlipUVs);
//	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
//		std::cerr << "Failed to load mesh: " << filepath << std::endl;
//		return false;
//	}
//	processNode(scene->mRootNode, scene);
//
//	//process mesh
//
//	//generate VertexArray from mesh
//	VertexObject = VertexArray::Create();
//	auto vbuffer = VertexBuffer::Create((float*)vertices.data(), vertices.size() * sizeof(VertexBase));
//	BufferLayout layout = {
//		BufferElement(ShaderDataType::Float3,"a_Position",false),
//	};
//	vbuffer->SetLayout(layout);
//	auto ibuffer = IndexBuffer::Create(indices.data(), indices.size());
//	VertexObject.AddVertexBuffer(vbuffer);
//	VertexObject.SetIndexBuffer(ibuffer);
//
//	return true;
//}
