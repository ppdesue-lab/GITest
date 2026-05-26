#include "stdsfx.h"
#include "Object3D.h"
#include <Renderer/RenderCommand.h>

#include <functional>
#include <fstream>
#include <cctype>

#include "Application.h"

#ifdef G_OPENGL
#include <glad/glad.h>
#endif


void Mesh::Draw(const glm::mat4& view, const glm::mat4 proj, const glm::mat4& parentTransform)
{
	const glm::mat4 model = parentTransform * Transfm.GetMatrix();
	Mat->Bind();
	Mat->MatShader->SetMat4("u_View", view);
	Mat->MatShader->SetMat4("u_Projection", proj);
	Mat->MatShader->SetMat4("u_Model", model);
	{
		//phong stuff
		glm::vec3 viewpos = glm::vec3(glm::inverse(view)[3]);
		Mat->MatShader->SetFloat3("u_lightPos", glm::vec3(0,10,0));
		Mat->MatShader->SetFloat3("u_viewPos", viewpos);
		Mat->MatShader->SetFloat3("u_lightColor", glm::vec3(0, 1, 0));
		Mat->MatShader->SetFloat3("u_objectColor", glm::vec3(1, 1, 1));

		// CSM shadow uniforms (set when shader supports them)
		Application& app = Application::Get();
		CSM& csm = app.GetCSM();
		auto& lightViewProj = csm.GetLightViewProjMatrices();
		auto& cascadeDists = csm.GetCascadeDistances();
		uint32_t cascadeCount = csm.Enabled() ? csm.GetCascadeCount() : 0;
		auto lightDir = csm.GetLight().Direction;

		Mat->MatShader->SetFloat3("u_lightPos", -glm::normalize(lightDir) * 1000.0f);
		Mat->MatShader->SetFloat3("u_lightColor", csm.GetLight().Color * csm.GetLight().Intensity);
		Mat->MatShader->SetInt("u_cascadeCount", (int)cascadeCount);
		Mat->MatShader->SetFloat("u_shadowMapSize", (float)csm.GetShadowMapSize());
		Mat->MatShader->SetFloat("u_shadowConstantBias", csm.ConstantBias());
		Mat->MatShader->SetFloat("u_shadowSlopeBias", csm.SlopeBias());
		Mat->MatShader->SetFloat3("u_lightDir", lightDir);
		Mat->MatShader->SetInt("u_debugCascadeView", Application::Get().GetDebugCascadeView() ? 1 : 0);
		for (uint32_t i = 0; i <= cascadeCount && i < 4; i++)
		{
			Mat->MatShader->SetFloat("u_cascadeDistances[" + std::to_string(i) + "]", cascadeDists[i]);
			if (i < cascadeCount)
				Mat->MatShader->SetMat4("u_lightViewProj[" + std::to_string(i) + "]", lightViewProj[i]);
		}

		csm.BindShadowTexture(2);
		Mat->MatShader->SetInt("u_shadowMap", 2);
		app.GetProbeGI().Bind(Mat->MatShader);
		app.GetPBRIBL().Bind(Mat->MatShader);
	}
#ifdef G_OPENGL
	Ref<ToonMaterial> toon = std::dynamic_pointer_cast<ToonMaterial>(Mat);
	if (toon && !toon->TwoSided)
	{
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}
#endif
    RenderCommand::DrawIndexed(VertexObject);
#ifdef G_OPENGL
	if (toon && toon->EdgeEnabled && toon->Alpha > 0.0f)
	{
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		toon->BindEdge(view, proj, model, Application::Get().GetViewportSize());
		RenderCommand::DrawIndexed(VertexObject);
	}
	if (toon)
		glDisable(GL_CULL_FACE);
#endif
};

void Object3D::Draw(const glm::mat4& view,const glm::mat4 proj)
{
	for (auto& mesh : Meshes)
		mesh->Draw(view, proj, Transfm.GetMatrix());
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

	auto loadEmbeddedTexture = [&](const aiTexture* texture, const std::string& key) -> Ref<Texture>
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
		return TextureLibrary::GetTexture(key, image);
	};

	auto loadMaterialTexture = [&](aiMaterial* material, std::initializer_list<aiTextureType> textureTypes) -> Ref<Texture>
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
				Ref<Texture> texture = loadEmbeddedTexture(embeddedTexture, filepath.u8string() + "#" + name);
				if (texture)
					INFO("Loaded embedded model texture: {}", name);
				else
					ERROR("Failed to decode embedded model texture: {}", name);
				return texture;
			}

			std::filesystem::path texturePath = std::filesystem::u8path(name);
			if (texturePath.is_relative())
				texturePath = filepath.parent_path() / texturePath;
			return TextureLibrary::GetTexture(texturePath.u8string());
		}
		return nullptr;
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
		mesh->VertexObject = VertexArray::Create();
		auto vbuffer = VertexBuffer::Create(reinterpret_cast<float*>(vertices.data()), vertices.size() * sizeof(T));
		vbuffer->SetLayout(layout);
		mesh->VertexObject->AddVertexBuffer(vbuffer);
		mesh->VertexObject->SetIndexBuffer(IndexBuffer::Create(indices.data(), indices.size()));
		mesh->VertexObject->Unbind();
		mesh->Mat = createMaterial(sourceMesh);
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
