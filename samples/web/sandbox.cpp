/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <filament/DebugRegistry.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>

#include <math/vec3.h>

#include <utils/Entity.h>
#include <utils/EntityManager.h>

#include <imgui.h>

#include "filamesh.h"
#include "filaweb.h"

using namespace filament;
using namespace filagui;
using namespace math;
using namespace utils;

using MagFilter = TextureSampler::MagFilter;
using WrapMode = TextureSampler::WrapMode;
using Format = Texture::InternalFormat;

// BEGIN COMMON

constexpr uint8_t MATERIAL_MODEL_UNLIT =       0;
constexpr uint8_t MATERIAL_MODEL_LIT =         1;
constexpr uint8_t MATERIAL_MODEL_SUBSURFACE =  2;
constexpr uint8_t MATERIAL_MODEL_CLOTH =       3;

constexpr uint8_t MATERIAL_UNLIT =       0;
constexpr uint8_t MATERIAL_LIT =         1;
constexpr uint8_t MATERIAL_SUBSURFACE =  2;
constexpr uint8_t MATERIAL_CLOTH =       3;
constexpr uint8_t MATERIAL_TRANSPARENT = 4;
constexpr uint8_t MATERIAL_FADE =        5;
constexpr uint8_t MATERIAL_COUNT =       6;

constexpr uint8_t BLENDING_OPAQUE      = 0;
constexpr uint8_t BLENDING_TRANSPARENT = 1;
constexpr uint8_t BLENDING_FADE        = 2;

struct SandboxParameters {
    sRGBColor color = sRGBColor{0.69f, 0.69f, 0.69f};
    float alpha = 1.0f;
    float roughness = 0.6f;
    float metallic = 0.0f;
    float reflectance = 0.5f;
    float clearCoat = 0.0f;
    float clearCoatRoughness = 0.0f;
    float anisotropy = 0.0f;
    float thickness = 1.0f;
    float subsurfacePower = 12.234f;
    sRGBColor subsurfaceColor = sRGBColor{0.0f};
    sRGBColor sheenColor = sRGBColor{0.83f, 0.0f, 0.0f};
    int currentMaterialModel = MATERIAL_MODEL_LIT;
    int currentBlending = BLENDING_OPAQUE;
    bool castShadows = true;
    sRGBColor lightColor = sRGBColor{0.98f, 0.92f, 0.89f};
    float lightIntensity = 110000.0f;
    float3 lightDirection = {0.6f, -1.0f, -0.8f};
    float iblIntensity = 30000.0f;
    float iblRotation = 0.0f;
    float sunHaloSize = 10.0f;
    float sunHaloFalloff = 80.0f;
    float sunAngularRadius = 1.9f;
    bool directional_light_enabled = true;
};

// END COMMON

struct SandboxApp {
    Filamesh filamesh;
    Material* mat;
    MaterialInstance* mi;
    Camera* cam;
    Entity sun;
    SandboxParameters params;
};

static constexpr uint8_t MATERIAL_LIT_PACKAGE[] = {
    #include "generated/material/sandboxLit.inc"
};

static SandboxApp app;

void setup(Engine* engine, View* view, Scene* scene) {

    // Create material.
    app.mat = Material::Builder()
            .package((void*) MATERIAL_LIT_PACKAGE, sizeof(MATERIAL_LIT_PACKAGE))
            .build(*engine);
    app.mi = app.mat->createInstance();
    app.mi->setParameter("clearCoat", 0.0f);

    // Move raw asset data from JavaScript to C++ static storage. Their held data will be freed via
    // BufferDescriptor callbacks after Filament creates the corresponding GPU objects.
    static auto mesh = filaweb::getRawFile("mesh");

    // Create mesh.
    const uint8_t* mdata = mesh.data.get();
    const auto destructor = [](void* buffer, size_t size, void* user) {
        auto asset = (filaweb::Asset*) user;
        asset->data.reset();
    };
    app.filamesh = decodeMesh(*engine, mdata, 0, app.mi, destructor, &mesh);
    scene->addEntity(app.filamesh->renderable);

    // Create the sun.
    auto& em = EntityManager::get();
    app.sun = em.create();
    LightManager::Builder(LightManager::Type::SUN)
            .color(Color::toLinear<ACCURATE>({ 0.98f, 0.92f, 0.89f }))
            .intensity(110000)
            .direction({ 0.7, -1, -0.8 })
            .sunAngularRadius(1.2f)
            .castShadows(true)
            .build(*engine, app.sun);
    scene->addEntity(app.sun);

    // Create skybox and image-based light source.
    auto skylight = filaweb::getSkyLight(*engine, "pillars_2k");
    scene->setIndirectLight(skylight.indirectLight);
    scene->setSkybox(skylight.skybox);

    const math::float3 center {0, 1.05, -1};
    const math::float3 eye {0, 1, 0};

    app.cam = engine->createCamera();
    app.cam->setExposure(16.0f, 1 / 125.0f, 100.0f);
    app.cam->lookAt(eye, center);
    view->setCamera(app.cam);
    view->setClearColor({0.1, 0.125, 0.25, 1.0});
};

void animate(Engine* engine, View* view, double now) {
    static double previous = now;

    // Adjust camera on every frame in case window size changes.
    using Fov = Camera::Fov;
    const uint32_t width = view->getViewport().width;
    const uint32_t height = view->getViewport().height;
    double ratio = double(width) / height;
    app.cam->setProjection(45.0, ratio, 0.1, 50.0, ratio < 1 ? Fov::HORIZONTAL : Fov::VERTICAL);

    // Spin the object.
    auto& tcm = engine->getTransformManager();
    tcm.setTransform(tcm.getInstance(app.filamesh->renderable),
        mat4f{mat3f{1.0}, float3{0.0f, 0.0f, -5.0f}} *
        mat4f::rotate(now, math::float3{0, 1, 0}));
};

void ui(Engine* engine, View* view) {
    auto& params = app.params;
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f));
    ImGui::Begin("Parameters");
    {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Combo("model", &params.currentMaterialModel,
                    "unlit\0lit\0subsurface\0cloth\0\0");

            if (params.currentMaterialModel == MATERIAL_MODEL_LIT) {
                ImGui::Combo("blending", &params.currentBlending,
                        "opaque\0transparent\0fade\0\0");
            }

            ImGui::ColorEdit3("baseColor", &params.color.r);

            if (params.currentMaterialModel > MATERIAL_MODEL_UNLIT) {
                if (params.currentBlending == BLENDING_TRANSPARENT ||
                        params.currentBlending == BLENDING_FADE) {
                    ImGui::SliderFloat("alpha", &params.alpha, 0.0f, 1.0f);
                }
                ImGui::SliderFloat("roughness", &params.roughness, 0.0f, 1.0f);
                if (params.currentMaterialModel != MATERIAL_MODEL_CLOTH) {
                    ImGui::SliderFloat("metallic", &params.metallic, 0.0f, 1.0f);
                    ImGui::SliderFloat("reflectance", &params.reflectance, 0.0f, 1.0f);
                }
                if (params.currentMaterialModel != MATERIAL_MODEL_CLOTH &&
                        params.currentMaterialModel != MATERIAL_MODEL_SUBSURFACE) {
                    ImGui::SliderFloat("clearCoat", &params.clearCoat, 0.0f, 1.0f);
                    ImGui::SliderFloat("clearCoatRoughness", &params.clearCoatRoughness, 0.0f, 1.0f);
                    ImGui::SliderFloat("anisotropy", &params.anisotropy, -1.0f, 1.0f);
                }
                if (params.currentMaterialModel == MATERIAL_MODEL_SUBSURFACE) {
                    ImGui::SliderFloat("thickness", &params.thickness, 0.0f, 1.0f);
                    ImGui::SliderFloat("subsurfacePower", &params.subsurfacePower, 1.0f, 24.0f);
                    ImGui::ColorEdit3("subsurfaceColor", &params.subsurfaceColor.r);
                }
                if (params.currentMaterialModel == MATERIAL_MODEL_CLOTH) {
                    ImGui::ColorEdit3("sheenColor", &params.sheenColor.r);
                    ImGui::ColorEdit3("subsurfaceColor", &params.subsurfaceColor.r);
                }
            }
        }

        if (ImGui::CollapsingHeader("Object")) {
            ImGui::Checkbox("castShadows", &params.castShadows);
        }

        if (ImGui::CollapsingHeader("Light")) {
            ImGui::Checkbox("enabled", &params.directional_light_enabled);
            ImGui::ColorEdit3("color", &params.lightColor.r);
            ImGui::SliderFloat("lux", &params.lightIntensity, 0.0f, 150000.0f);
            ImGui::SliderFloat3("direction", &params.lightDirection.x, -1.0f, 1.0f);
            ImGui::SliderFloat("sunSize", &params.sunAngularRadius, 0.1f, 10.0f);
            ImGui::SliderFloat("haloSize", &params.sunHaloSize, 1.01f, 40.0f);
            ImGui::SliderFloat("haloFalloff", &params.sunHaloFalloff, 0.0f, 2048.0f);
            ImGui::SliderFloat("ibl", &params.iblIntensity, 0.0f, 50000.0f);
            ImGui::SliderAngle("ibl rotation", &params.iblRotation);
        }

        if (ImGui::CollapsingHeader("Debug")) {
            DebugRegistry& debug = engine->getDebugRegistry();
            ImGui::Checkbox("Light Far uses shadow casters",
                    debug.getPropertyAddress<bool>("d.shadowmap.far_uses_shadowcasters"));
            ImGui::Checkbox("Focus shadow casters",
                    debug.getPropertyAddress<bool>("d.shadowmap.focus_shadowcasters"));
            bool* lispsm;
            if (debug.getPropertyAddress<bool>("d.shadowmap.lispsm", &lispsm)) {
                ImGui::Checkbox("Enable LiSPSM", lispsm);
                if (*lispsm) {
                    ImGui::SliderFloat("dzn",
                            debug.getPropertyAddress<float>("d.shadowmap.dzn"), 0.0f, 1.0f);
                    ImGui::SliderFloat("dzf",
                            debug.getPropertyAddress<float>("d.shadowmap.dzf"),-1.0f, 0.0f);
                }
            }
        }
    }
    ImGui::End();

    int material = params.currentMaterialModel;
    if (material == MATERIAL_MODEL_LIT) {
        if (params.currentBlending == BLENDING_TRANSPARENT) material = MATERIAL_TRANSPARENT;
        if (params.currentBlending == BLENDING_FADE) material = MATERIAL_FADE;
    }
    MaterialInstance* materialInstance = app.mi; // params.materialInstance[material]
    if (params.currentMaterialModel == MATERIAL_MODEL_UNLIT) {
        materialInstance->setParameter("baseColor", RgbType::sRGB, params.color);
    }
    if (params.currentMaterialModel == MATERIAL_MODEL_LIT) {
        materialInstance->setParameter("baseColor", RgbType::sRGB, params.color);
        materialInstance->setParameter("roughness", params.roughness);
        materialInstance->setParameter("metallic", params.metallic);
        materialInstance->setParameter("reflectance", params.reflectance);
        materialInstance->setParameter("clearCoat", params.clearCoat);
        materialInstance->setParameter("clearCoatRoughness", params.clearCoatRoughness);
        materialInstance->setParameter("anisotropy", params.anisotropy);
        if (params.currentBlending != BLENDING_OPAQUE) {
            materialInstance->setParameter("alpha", params.alpha);
        }
    }
    if (params.currentMaterialModel == MATERIAL_MODEL_SUBSURFACE) {
        materialInstance->setParameter("baseColor", RgbType::sRGB, params.color);
        materialInstance->setParameter("roughness", params.roughness);
        materialInstance->setParameter("metallic", params.metallic);
        materialInstance->setParameter("reflectance", params.reflectance);
        materialInstance->setParameter("thickness", params.thickness);
        materialInstance->setParameter("subsurfacePower", params.subsurfacePower);
        materialInstance->setParameter("subsurfaceColor", RgbType::sRGB, params.subsurfaceColor);
    }
    if (params.currentMaterialModel == MATERIAL_MODEL_CLOTH) {
        materialInstance->setParameter("baseColor", RgbType::sRGB, params.color);
        materialInstance->setParameter("roughness", params.roughness);
        materialInstance->setParameter("sheenColor", RgbType::sRGB, params.sheenColor);
        materialInstance->setParameter("subsurfaceColor", RgbType::sRGB, params.subsurfaceColor);
    }
}

// This is called only after the JavaScript layer has created a WebGL 2.0 context and all assets
// have been downloaded.
extern "C" void launch() {
    filaweb::Application::get()->run(setup, animate, ui);
}

// The main() entry point is implicitly called after JIT compilation, but potentially before the
// WebGL context has been created or assets have finished loading.
int main() { }

