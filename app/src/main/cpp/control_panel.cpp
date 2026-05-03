#include "control_panel.h"

#include <cmath>
#include <cstring>

namespace vrp {

namespace {
struct V { float x, y, z, u, v; };

// Quad in panel-local space, centred at origin, facing -Z, axis-aligned.
constexpr V kQuad[4] = {
    { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f },
    {  0.5f, -0.5f, 0.0f, 1.0f, 0.0f },
    {  0.5f,  0.5f, 0.0f, 1.0f, 1.0f },
    { -0.5f,  0.5f, 0.0f, 0.0f, 1.0f },
};
constexpr uint32_t kQuadIdx[6] = { 0, 1, 2, 0, 2, 3 };
}  // namespace

void ControlPanel::Create() {
    Destroy();
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kQuadIdx), kQuadIdx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(offsetof(V, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), reinterpret_cast<void*>(offsetof(V, u)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    indexCount_ = 6;
    LOGI("ControlPanel created");
}

void ControlPanel::Destroy() {
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    indexCount_ = 0;
}

gl::Mat4 ControlPanel::ModelTransform() const {
    gl::Mat4 scale = gl::MakeScale(width_, height_, 1.0f);
    gl::Mat4 translate = gl::MakeTranslation(0.0f, verticalOffset_, -distance_);
    return gl::Multiply(translate, scale);
}

void ControlPanel::Draw(GLuint program, const gl::Mat4& view0, const gl::Mat4& view1,
                       const gl::Mat4& proj0, const gl::Mat4& proj1, const HitResult& hover) {
    if (!program || !vao_) return;

    glUseProgram(program);

    GLint locView  = glGetUniformLocation(program, "u_view");
    GLint locProj  = glGetUniformLocation(program, "u_proj");
    GLint locModel = glGetUniformLocation(program, "u_model");
    GLint locProgress = glGetUniformLocation(program, "u_progress");
    GLint locPlaying  = glGetUniformLocation(program, "u_isPlaying");
    GLint locLoading  = glGetUniformLocation(program, "u_isLoading");
    GLint locPointerU = glGetUniformLocation(program, "u_pointerU");
    GLint locPointerV = glGetUniformLocation(program, "u_pointerV");

    float views[32];
    std::memcpy(views,      view0.m, sizeof(view0.m));
    std::memcpy(views + 16, view1.m, sizeof(view1.m));
    glUniformMatrix4fv(locView, 2, GL_FALSE, views);

    float projs[32];
    std::memcpy(projs,      proj0.m, sizeof(proj0.m));
    std::memcpy(projs + 16, proj1.m, sizeof(proj1.m));
    glUniformMatrix4fv(locProj, 2, GL_FALSE, projs);

    gl::Mat4 model = ModelTransform();
    glUniformMatrix4fv(locModel, 1, GL_FALSE, model.m);

    glUniform1f(locProgress, progress_);
    glUniform1i(locPlaying,  isPlaying_ ? 1 : 0);
    glUniform1i(locLoading,  isLoading_ ? 1 : 0);
    glUniform1f(locPointerU, hover.u);
    glUniform1f(locPointerV, hover.v);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

ControlPanel::HitResult ControlPanel::HitTestRay(float ox, float oy, float oz,
                                                 float dx, float dy, float dz) const {
    HitResult r;
    // Plane in world space: panel centred at (0, verticalOffset, -distance_)
    // and parallel to XY → plane equation z = -distance_.
    if (std::abs(dz) < 1e-5f) return r;
    float t = (-distance_ - oz) / dz;
    if (t <= 0.0f) return r;
    float px = ox + dx * t;
    float py = oy + dy * t;
    float halfW = width_ * 0.5f;
    float halfH = height_ * 0.5f;
    float localX = px;
    float localY = py - verticalOffset_;
    if (localX < -halfW || localX > halfW || localY < -halfH || localY > halfH) return r;

    r.u = (localX + halfW) / width_;
    r.v = (localY + halfH) / height_;
    if (r.u < 0.15f) {
        r.region = Region_Minus15;
    } else if (r.u < 0.30f) {
        r.region = Region_PlayPause;
    } else if (r.u < 0.45f) {
        r.region = Region_Plus15;
    } else if (r.u >= 0.50f) {
        r.region = Region_Scrubber;
        r.scrubFraction = (r.u - 0.50f) / 0.50f;
    }
    return r;
}

ControlPanel::HitResult ControlPanel::HitTestGaze(const gl::Mat4& headView) const {
    // Compose head world transform from the inverse of the view matrix.
    // headView transforms world -> head; so to get the head pose in the world,
    // we invert. For a rigid view matrix, R_inv = transpose(R) and t_inv = -R^T t.
    const float* m = headView.m;
    float rx0 = m[0], rx1 = m[4], rx2 = m[8];
    float ry0 = m[1], ry1 = m[5], ry2 = m[9];
    float rz0 = m[2], rz1 = m[6], rz2 = m[10];
    float tx = m[12], ty = m[13], tz = m[14];

    // World-space head position = -R^T * t.
    float ox = -(rx0 * tx + ry0 * ty + rz0 * tz);
    float oy = -(rx1 * tx + ry1 * ty + rz1 * tz);
    float oz = -(rx2 * tx + ry2 * ty + rz2 * tz);
    // Forward in head-space is (0,0,-1); transform back into world via R^T.
    float fx = -rz0;
    float fy = -rz1;
    float fz = -rz2;
    float len = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (len > 0.0f) { fx /= len; fy /= len; fz /= len; }

    return HitTestRay(ox, oy, oz, fx, fy, fz);
}

}  // namespace vrp
