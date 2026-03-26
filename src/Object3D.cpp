#include "stdsfx.h"
#include "Object3D.h"
#include <Renderer/RenderCommand.h>

#include <functional>
#include "ResourceMesh.h"


void Mesh::Draw(const glm::mat4& view,const glm::mat4 proj)
{
	Mat->Bind();
	Mat->MatShader->SetMat4("u_View", view);
	Mat->MatShader->SetMat4("u_Projection", proj);
	//set model
	Mat->MatShader->SetMat4("u_Model", Transfm.GetMatrix());
	{
		//phong stuff
		glm::vec3 viewpos = glm::vec3(view[3]);
		Mat->MatShader->SetFloat3("u_lightPos", glm::vec3(0,10,0));
		Mat->MatShader->SetFloat3("u_viewPos", viewpos);
		Mat->MatShader->SetFloat3("u_lightColor", glm::vec3(0, 1, 0));
		Mat->MatShader->SetFloat3("u_objectColor", glm::vec3(1, 1, 1));

	}
    RenderCommand::DrawIndexed(VertexObject);
};

void Object3D::Draw(const glm::mat4& view,const glm::mat4 proj)
{
	for (auto& mesh : Meshes)
		mesh->Draw(view,proj);
}


#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

template<typename T>
bool Object3D::Load(const std::string& filepath) { 
	
	std::vector<T> vertices;
	std::vector<uint32_t> indices;
	auto processMesh = [&](aiMesh* mesh, const aiScene* scene) {

		uint32_t indices_offset = indices.size();
			//vector<Texture> textures;
			for (unsigned int i = 0; i < mesh->mNumVertices; i++)
			{
				// process vertex positions, normals and texture coordinates
				aiVector3D position = mesh->mVertices[i];
				T vertex;
				vertex.Position = glm::vec3(position.x, position.y, position.z);
				if constexpr (std::is_same_v<T, VertexColor>)
				{
					vertex.Color = glm::vec3(1, 0, 0);
				}
				else if constexpr (std::is_same_v<T, VertexNormal>)
				{
					vertex.Normal = glm::vec3(1, 0, 0);

					if (mesh->mNormals)
					{
						vertex.Normal.x = mesh->mNormals[i].x;
						vertex.Normal.y = mesh->mNormals[i].y;
						vertex.Normal.z = mesh->mNormals[i].z;
					}
				}
				else if constexpr (std::is_same_v<T, VertexTexture>)
				{
					vertex.TexCoord = glm::vec2(0, 0);

					if (mesh->mTextureCoords)
					{
						vertex.TexCoord.x = mesh->mTextureCoords[i].x;
						vertex.NorTexCoordmal.y = mesh->mTextureCoords[i].y;
					}
				}
				else if constexpr (std::is_same_v<T, VertexNormalTexture>)
				{
					vertex.Normal = glm::vec3(1, 0, 0);

					if (mesh->mNormals)
					{
						vertex.Normal.x = mesh->mNormals[i].x;
						vertex.Normal.y = mesh->mNormals[i].y;
						vertex.Normal.z = mesh->mNormals[i].z;
					}

					vertex.TexCoord = glm::vec2(0, 0);

					if (mesh->mTextureCoords)
					{
						vertex.TexCoord.x = mesh->mTextureCoords[i].x;
						vertex.NorTexCoordmal.y = mesh->mTextureCoords[i].y;
					}
				}
				vertices.push_back(vertex);
			}
			// process indices
			for (uint32_t i = 0; i < mesh->mNumFaces; i++)
			{
				aiFace face = mesh->mFaces[i];
				for (uint32_t j = 0; j < face.mNumIndices; j++)
					indices.push_back(indices_offset + face.mIndices[j]);
			}
		};
	
	std::function<void(aiNode* node, const aiScene* scene)> processNode;
	processNode = [&](aiNode* node, const aiScene* scene)
	{
		// process all the node's meshes (if any)
		for (unsigned int i = 0; i < node->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			processMesh(mesh, scene);
		}
		// then do the same for each of its children
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			processNode(node->mChildren[i], scene);
		}
	};
	
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		std::cerr << "Failed to load mesh: " << filepath << std::endl;
		return false;
	}
	processNode(scene->mRootNode, scene);
	
	//process mesh
	
	//generate VertexArray from mesh
	Ref<Mesh> mesh = CreateRef<Mesh>();
	auto VertexObject = VertexArray::Create();
	auto vbuffer = VertexBuffer::Create((float*)vertices.data(), vertices.size() * sizeof(T));
	BufferLayout layout;
	Ref<Material> meshmat = nullptr;
	if constexpr (std::is_same_v<T, VertexColor>)
	{
		layout = {
			BufferElement(ShaderDataType::Float3,"a_Position",false),
			BufferElement(ShaderDataType::Float3,"a_Color",false),
		};

		meshmat = CreateRef<MaterialColor>();
	}
	else if constexpr (std::is_same_v<T, VertexNormal>)
	{
		layout = {
			BufferElement(ShaderDataType::Float3,"a_Position",false),
			BufferElement(ShaderDataType::Float3,"a_Normal",false),
		};
		meshmat = CreateRef<MaterialPhong>();
	}

	vbuffer->SetLayout(layout);
	auto ibuffer = IndexBuffer::Create(indices.data(), indices.size());
	VertexObject->AddVertexBuffer(vbuffer);
	VertexObject->SetIndexBuffer(ibuffer);
	VertexObject->Unbind();
	mesh->VertexObject = VertexObject;

	mesh->Mat = meshmat;
	Meshes.clear();
	Meshes.push_back(mesh);

	return true;

}

//template bool Object3D::Load<VertexBase>(const std::string& filepath);
template bool Object3D::Load<VertexColor>(const std::string& filepath);
template bool Object3D::Load<VertexNormal>(const std::string& filepath);


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