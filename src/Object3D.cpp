#include "stdsfx.h"
#include "Object3D.h"
#include <Renderer/RenderCommand.h>

void Object3D::Draw(const glm::mat4& view,const glm::mat4 proj)
{
	Mat->Bind();
	Mat->MatShader->SetMat4("u_View", view);
	Mat->MatShader->SetMat4("u_Projection", proj);
	//set model
	Mat->MatShader->SetMat4("u_Model", Transfm.GetMatrix());

    RenderCommand::DrawIndexed(VertexObject);
};