/*
 * Copyright 2020 Adobe. All rights reserved.
 * This file is licensed to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
 * OF ANY KIND, either express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 */
#include <lagrange/ui/Viewer.h>
#include <lagrange/ui/components/ObjectIDViewport.h>
#include <lagrange/ui/components/SelectionViewport.h>
#include <lagrange/ui/default_entities.h>
#include <lagrange/ui/default_events.h>
#include <lagrange/ui/default_ibls.h>
#include <lagrange/ui/default_panels.h>
#include <lagrange/ui/default_shaders.h>
#include <lagrange/ui/default_systems.h>
#include <lagrange/ui/default_tools.h>
#include <lagrange/ui/panels/RendererPanel.h>
#include <lagrange/ui/panels/ToolbarPanel.h>
#include <lagrange/ui/panels/ViewportPanel.h>
#include <lagrange/ui/systems/camera_systems.h>
#include <lagrange/ui/systems/render_geometry.h>
#include <lagrange/ui/systems/render_shadowmaps.h>
#include <lagrange/ui/systems/render_viewports.h>
#include <lagrange/ui/systems/update_accelerated_picking.h>
#include <lagrange/ui/systems/update_lights.h>
#include <lagrange/ui/systems/update_mesh_bounds.h>
#include <lagrange/ui/systems/update_mesh_buffers.h>
#include <lagrange/ui/systems/update_mesh_elements_hovered.h>
#include <lagrange/ui/systems/update_mesh_hovered.h>
#include <lagrange/ui/systems/update_scene_bounds.h>
#include <lagrange/ui/systems/update_transform_hierarchy.h>
#include <lagrange/ui/types/GLContext.h>
#include <lagrange/ui/utils/bounds.h>
#include <lagrange/ui/utils/events.h>
#include <lagrange/ui/utils/file_dialog.h>
#include <lagrange/ui/utils/ibl.h>
#include <lagrange/ui/utils/io.h>
#include <lagrange/ui/utils/mesh.h>
#include <lagrange/ui/utils/mesh.impl.h>
#include <lagrange/ui/utils/selection.h>
#include <lagrange/ui/utils/treenode.h>
#include <lagrange/ui/utils/viewport.h>
#include <lagrange/ui/utils/lights.h>
#include <lagrange/ui/utils/colormap.h>


// Imgui and related
#include <IconsFontAwesome5.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <fonts/fontawesome5.h>
#include <imgui.h>
#include <imgui_internal.h> //todo move dock stuff to uiwindow system
#include <misc/cpp/imgui_stdlib.h>

#include <stdio.h>
#include <fstream>


#define MODAL_NAME_SHADER_ERROR "Shader Error"

namespace lagrange {
namespace ui {


bool Viewer::is_key_down(int key)
{
    return !ImGui::IsAnyItemActive() && ImGui::IsKeyDown(key);
}

bool Viewer::is_key_pressed(int key)
{
    return !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(key);
}

bool Viewer::is_key_released(int key)
{
    return !ImGui::IsAnyItemActive() && ImGui::IsKeyReleased(key);
}

bool Viewer::is_mouse_down(int key)
{
    return !ImGui::IsAnyItemActive() && ImGui::IsMouseDown(key);
}

bool Viewer::is_mouse_clicked(int key)
{
    return !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(key);
}

bool Viewer::is_mouse_released(int key)
{
    return !ImGui::IsAnyItemActive() && ImGui::IsMouseReleased(key);
}

Viewer::Viewer(const std::string& window_title, int window_width, int window_height)
    : Viewer(WindowOptions{window_title, -1, -1, window_width, window_height})
{}

Viewer::Viewer(const WindowOptions& window_options)
    : m_initial_window_options(window_options)
{
    if (window_options.imgui_ini_path.empty()) {
        m_imgui_ini_path = get_config_folder() + m_initial_window_options.window_title + ".ini";
    } else {
        m_imgui_ini_path = window_options.imgui_ini_path;
    }

    {
        lagrange::fs::path folder = lagrange::fs::path(m_imgui_ini_path).parent_path();
        if (!lagrange::fs::exists(folder)) {
            if (!lagrange::fs::create_directories(folder)) {
                lagrange::logger().error("Cannot create folder {} for UI settings", folder);
            }
        }
    }

    if (!init_glfw(window_options)) return;
    if (!init_imgui()) return;

    update_scale();

    register_default_component_widgets();


    entt::meta<lagrange::MeshBase>().type();

    // entt::meta<lagrange::MeshBase>().func<>("get_mesh_vertices"_hs);

    ui::register_mesh_type<lagrange::TriangleMesh3Df>();
    ui::register_mesh_type<lagrange::TriangleMesh3D>();

    /*
        System registration
    */
    using DS = DefaultSystems;

    // Initialization of the frame
    m_systems.add(
        Systems::Stage::Init,
        std::bind(&Viewer::make_current, this),
        DS::MakeContextCurrent);

    m_systems.add(Systems::Stage::Init, std::bind(&Viewer::update_time, this), DS::UpdateTime);
    m_systems.add(Systems::Stage::Init, std::bind(&Viewer::process_input, this), DS::ProcessInput);


    // Make sure that selection/outline and objectid viewports have correct size and post process effects
    m_systems.add(
        Systems::Stage::Init,
        [](Registry& r) {
            auto focused_viewport_entity = ui::get_focused_viewport_entity(r);
            auto selection_viewport_entity = ui::get_selection_viewport_entity(r);

            auto& focused = r.get<ViewportComponent>(focused_viewport_entity);
            auto& selection = r.get<ViewportComponent>(selection_viewport_entity);

            auto object_mode =
                get_selection_context(r).element_type == entt::resolve<ElementObject>().id();

            // If there's none, check all components first and delete them, then add it to this one
            if (focused.post_process_effects.count("SelectionOutline") == 0 || !object_mode) {
                const auto& view = r.view<ViewportComponent>();
                for (auto e : view) {
                    view.get<ViewportComponent>(e).post_process_effects.erase("SelectionOutline");
                }

                // Only show outline if in object mode
                if (object_mode) {
                    add_selection_outline_post_process(r, focused_viewport_entity);
                }
            }


            selection.camera_reference = focused.camera_reference;
            selection.width = focused.width;
            selection.height = focused.height;


            // Sort viewports so that selection is before focused
            r.sort<ViewportComponent>([=](Entity a, Entity b) {
                if (a == selection_viewport_entity) return true;
                return a < b;
            });
        },
        DS::UpdateSelectionOutlineViewport);

    // Selection render layer
    m_systems.add(
        Systems::Stage::Init,
        [](Registry& r) {
            r.view<Layer>().each([&](Entity e, Layer& /*s*/) {
                if (is_in_layer(r, e, DefaultLayers::Selection)) {
                    if (!is_selected(r, e)) {
                        remove_from_layer(r, e, DefaultLayers::Selection);
                    }
                }
                if (is_in_layer(r, e, DefaultLayers::Hover)) {
                    if (!is_hovered(r, e)) {
                        remove_from_layer(r, e, DefaultLayers::Hover);
                    }
                }
            });

            selected_view(r).each(
                [&](Entity e, Selected& /*s*/) { add_to_layer(r, e, DefaultLayers::Selection); });

            hovered_view(r).each(
                [&](Entity e, Hovered& /*s*/) { add_to_layer(r, e, DefaultLayers::Hover); });
        },
        DS::UpdateSelectedRenderLayer);

    m_systems.add(
        Systems::Stage::Init,
        [](Registry& r) {
            // Toggle selected and hovered on_construct and on_destroy listeners
            toggle_component_event<Selected, SelectedEvent, DeselectedEvent>(r);
            toggle_component_event<Hovered, HoveredEvent, DehoveredEvent>(r);

            // Reload shader, etc.
            if (lagrange::ui::get_keybinds(r).is_released("global.reload")) {
                get_shader_cache(r).clear();
                lagrange::logger().info("Cleared shader cache.");
            }
        },
        DS::InitMisc);


    // Draw all
    m_systems.add(
        Systems::Stage::Interface,
        [](Registry& r) {
            auto view = r.view<UIPanel>();
            for (auto e : view) {
                auto& window = view.get<UIPanel>(e);

                // Skip hidden windows
                if (!window.visible) continue;

                if (window.static_position_enabled) {
                    ImGui::SetNextWindowPos(
                        ImVec2(window.static_position.x(), window.static_position.y()));
                }
                if (window.static_size_enabled) {
                    ImGui::SetNextWindowSize(
                        ImVec2(window.static_size.x(), window.static_size.y()));
                }

                if (window.before_fn) window.before_fn(r, e);

                if (begin_panel(window) && window.body_fn) {
                    window.body_fn(r, e);
                }

                end_panel(window);

                if (window.after_fn) window.after_fn(r, e);
            }
        },
        DS::DrawUIPanels);

    m_systems.add(Systems::Stage::Interface, &update_mesh_hovered, DS::UpdateMeshHovered);

    m_systems.add(
        Systems::Stage::Interface,
        &update_mesh_elements_hovered,
        DS::UpdateMeshElementsHovered);

    m_systems.add(
        Systems::Stage::Interface,
        [=](Registry& r) { r.ctx<Tools>().run_current(r); },
        DS::RunCurrentTool);

    m_systems.add(Systems::Stage::Interface, &update_lights_system, DS::UpdateLights);

    // Simulation stage systems
    m_systems.add(
        Systems::Stage::Simulation,
        &update_transform_hierarchy,
        DS::UpdateTransformHierarchy);
    m_systems.add(Systems::Stage::Simulation, &update_mesh_bounds_system, DS::UpdateMeshBounds);
    m_systems.add(Systems::Stage::Simulation, &update_scene_bounds_system, DS::UpdateSceneBounds);
    m_systems.add(
        Systems::Stage::Simulation,
        &camera_controller_system,
        DS::UpdateCameraController);
    m_systems.add(Systems::Stage::Simulation, &camera_turntable_system, DS::UpdateCameraTurntable);
    m_systems.add(Systems::Stage::Simulation, &camera_focusfit_system, DS::UpdateCameraFocusFit);


    m_systems.add(
        Systems::Stage::Render,
        &update_accelerated_picking,
        DS::UpdateAcceleratedPicking);
    m_systems.add(Systems::Stage::Render, &update_mesh_buffers_system, DS::UpdateMeshBuffers);
    m_systems.add(Systems::Stage::Render, &setup_vertex_data, DS::SetupVertexData);
    m_systems.add(Systems::Stage::Render, &render_shadowmaps, DS::RenderShadowMaps);
    m_systems.add(Systems::Stage::Render, &render_viewports, DS::RenderViewports);


    // Remove mesh dirty flag -> its now propagated
    m_systems.add(
        Systems::Stage::Post,
        [](Registry& r) { r.clear<MeshDataDirty>(); },
        DS::ClearDirtyFlags);


    // onDestroy behavior for Tree component -> ensure correctness
    m_registry.on_destroy<TreeNode>().connect<&orphan_without_subtree>();


    /*
        Entity and context variable initialization
    */
    // Initialize InputState context variable
    {
        InputState input;
        input.keybinds = std::make_shared<Keybinds>(initialize_default_keybinds());
        registry().set<InputState>(std::move(input));
    }


    // Layer names
    register_default_layer_names(registry());

    registry().set<WindowSize>(WindowSize{window_options.width, window_options.height});

    // Tool context
    auto& tools = registry().set<Tools>(Tools{});
    register_default_tools(tools);

    // Shaders
    register_default_shaders(registry());

    // Scene bounds
    registry().set<Bounds>(Bounds());

    registry().create(); // Create zero'th entity

    // UI windows - create and set as context variable
    auto& windows = m_registry.set<DefaultPanels>(add_default_panels(registry()));

    m_width = window_options.width;
    m_height = window_options.height;


    // Create default camera
    auto default_camera =
        add_camera(registry(), Camera::default_camera(float(m_width), float(m_height)));


    // Set up offscreen viewports

    auto selection_viewport = [&]() {
        auto& r = registry();
        auto viewport = add_viewport(r, default_camera);
        r.emplace_or_replace<Name>(viewport, "Selection Mask Viewport");

        // Set viewport to see only selection layer
        auto& vc = r.get<ViewportComponent>(viewport);
        vc.visible_layers.reset().set(DefaultLayers::Selection, true);
        vc.material_override = create_material(r, DefaultShaders::Simple);
        vc.material_override->set_color("in_color", Color::red());
        vc.material_override->set_int(RasterizerOptions::Primitive, GL_TRIANGLES);
        return viewport;
    }();
    registry().set<SelectionViewport>(SelectionViewport{selection_viewport});


    auto object_id_viewport = [&]() {
        auto& r = registry();
        auto viewport = add_viewport(r, default_camera);
        r.emplace_or_replace<Name>(viewport, "ObjectID Viewport");

        auto& vc = r.get<ViewportComponent>(viewport);
        vc.enabled = false; // Off by default
        vc.material_override = create_material(r, DefaultShaders::ObjectID);
        vc.material_override->set_int(RasterizerOptions::Primitive, GL_TRIANGLES);
        return viewport;
    }();
    // Set as context variable
    registry().set<ObjectIDViewport>(ObjectIDViewport{object_id_viewport});


    // Set up default viewport
    {
        // Create viewport
        auto main_viewport = add_viewport(registry(), default_camera, true);
        
        registry().emplace_or_replace<Name>(main_viewport, "Default Viewport");

        // Set the viewport to default viewport window
        registry().get<ViewportPanel>(windows.viewport).viewport = main_viewport;
    }


    // Sort viewports (manually for now)
    {
        registry().sort<ViewportComponent>([&](Entity a, Entity b) {
            if (a == m_main_viewport) return false;
            return a < b;
        });
    }

    m_initialized = true;
    m_instance_initialized = true;

    /*
        Load default ibl
    */
    if (window_options.default_ibl != "") {
        // Todo extract out into public API
        try {
            auto ibl = ui::generate_default_ibl(
                window_options.default_ibl,
                window_options.default_ibl_resolution);
            ibl.blur = 2.0f;
            add_ibl(registry(), std::move(ibl));

        } catch (const std::exception& ex) {
            lagrange::logger().error("Failed to generate ibl: {}", ex.what());
        }
    }
}


bool Viewer::run(const std::function<bool(Registry&)>& main_loop)
{
    if (!is_initialized()) return false;

    while (!should_close()) {
        for (unsigned int fn_counter = 0; fn_counter < m_main_thread_max_func_per_frame; fn_counter++) {
            std::pair<std::promise<void>, std::function<void(void)>> item;
            {
                // Consume queue item
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_main_thread_fn.empty()) break;
                item = std::move(m_main_thread_fn.front());
                m_main_thread_fn.pop();
            }
            // Run function
            item.second();
            // Set promise value
            item.first.set_value();
        }


        {
            /*
                Initialization systems
            */
            m_systems.run(Systems::Stage::Init, registry());


            start_imgui_frame();


            draw_menu();
            // Dock space
            {
                start_dockspace();
                if (m_show_imgui_demo) {
                    ImGui::ShowDemoWindow(&m_show_imgui_demo);
                }

                if (m_show_imgui_style) {
                    ImGui::Begin("Style Editor", &m_show_imgui_style);
                    ImGui::ShowStyleEditor();
                    ImGui::End();
                }

                m_systems.run(Systems::Stage::Interface, registry());

                if (main_loop) {
                    if (!main_loop(registry())) glfwSetWindowShouldClose(m_window, true);
                }

                show_last_shader_error();
                end_dockspace();
            }

            end_imgui_frame();


            m_systems.run(Systems::Stage::Simulation, registry());


            // All rendering goes here
            {
                /*
                    Render to texture
                */
                try {
                    m_last_shader_error = "";
                    m_last_shader_error_desc = "";

                    m_systems.run(Systems::Stage::Render, registry());

                } catch (ShaderException& ex) {
                    m_last_shader_error = ex.what();
                    m_last_shader_error_desc = ex.get_desc();
                    lagrange::logger().error("{}", m_last_shader_error);
                }

                /*
                    Clear default framebuffer
                */
                GLScope gl;
                {
                    gl(glBindFramebuffer, GL_FRAMEBUFFER, 0);
                    gl(glViewport, 0, 0, m_width, m_height);
                    Color bgcolor = Color(0, 0, 0, 0);
                    gl(glClearColor, bgcolor.x(), bgcolor.y(), bgcolor.z(), bgcolor.a());
                    gl(glClear, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }


                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); // render to screen buffer
            }


            glfwSwapBuffers(m_window);
        }


        m_systems.run(Systems::Stage::Post, registry());
    }

    return true;
}


bool Viewer::run(const std::function<void(void)>& main_loop /*= {}*/)
{
    return run([=](Registry & /*r*/) -> bool {
        if (main_loop) main_loop();
        return true;
    });
}

bool Viewer::should_close() const
{
    return glfwWindowShouldClose(m_window);
}


bool Viewer::is_initialized() const
{
    return m_initialized;
}


double Viewer::get_frame_elapsed_time() const
{
    return ImGui::GetIO().DeltaTime;
}


const std::string& Viewer::get_imgui_config_path() const
{
    return m_imgui_ini_path;
}

float Viewer::get_window_scaling() const
{
    return m_ui_scaling;
}

Keybinds& Viewer::get_keybinds()
{
    return lagrange::ui::get_keybinds(m_registry);
}

const Keybinds& Viewer::get_keybinds() const
{
    return lagrange::ui::get_keybinds(m_registry);
}


Viewer::~Viewer()
{
    publish<WindowCloseEvent>(m_registry);

    // Explicitly remove scene and renderer first before closing
    // the opengl context
    m_registry = {};


    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(m_imgui_context);
    glfwDestroyWindow(m_window);
    glfwTerminate(); // TODO ref count windows
}

bool Viewer::init_glfw(const WindowOptions& options)
{
    glfwSetErrorCallback(
        [](int error, const char* msg) { logger().error("GLFW Error {}: {}", error, msg); });

    if (!glfwInit()) {
        logger().error("Failed to initialize GLFW");
        return false;
    }

    GLState::major_version = options.gl_version_major;
    GLState::minor_version = options.gl_version_minor;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, GLState::major_version);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, GLState::minor_version);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (options.gl_version_major > 3 && options.gl_version_minor > 1) {
        // previous two hints were here but that caused crashes under osx...
    }

    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GL_TRUE);

    glfwWindowHint(GLFW_FOCUSED, options.focus_on_show ? GLFW_TRUE : GLFW_FALSE);

    const char* glfw_version = glfwGetVersionString();
    logger().info("GLFW compile time version: {}", glfw_version);
    logger().info(
        "Requested context: {}.{}, GLSL {}",
        GLState::major_version,
        GLState::minor_version,
        GLState::get_glsl_version_string());

    int monitor_count = 0;
    int monitor_index = options.monitor_index;
    GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);
    if (options.monitor_index > monitor_count) monitor_index = 0;

    int width = options.width;
    int height = options.height;

    m_window = glfwCreateWindow(
        width,
        height,
        options.window_title.c_str(),
        (options.fullscreen) ? monitors[options.monitor_index] : nullptr,
        nullptr);

    if (!m_window) {
        logger().error("Failed to create window");
        return false;
    }

    int xpos, ypos;
    glfwGetMonitorPos(monitors[monitor_index], &xpos, &ypos);

    int workarea_x, workarea_y, screen_res_x, screen_res_y;
    glfwGetMonitorWorkarea(
        monitors[monitor_index],
        &workarea_x,
        &workarea_y,
        &screen_res_x,
        &screen_res_y);

    // Center by default
    int user_x_pos = (options.pos_x != -1) ? options.pos_x : (screen_res_x - width) / 2;
    int user_y_pos = (options.pos_y != -1) ? options.pos_y : (screen_res_y - height) / 2;

    if (options.window_fullscreen) {
        glfwMaximizeWindow(m_window);
    } else {
        xpos += user_x_pos;
        ypos += user_y_pos;
        glfwSetWindowPos(m_window, xpos, ypos);
    }


    assert(m_window);
    glfwMakeContextCurrent(m_window);
    assert(m_window);

    gl3wInit();

    glfwSwapInterval(options.vsync ? 1 : 0);

    {
        float xscale, yscale;
        glfwGetWindowContentScale(m_window, &xscale, &yscale);

        int client_api = glfwGetWindowAttrib(m_window, GLFW_CLIENT_API);
        int creation_api = glfwGetWindowAttrib(m_window, GLFW_CONTEXT_CREATION_API);
        int v_major = glfwGetWindowAttrib(m_window, GLFW_CONTEXT_VERSION_MAJOR);
        int v_minor = glfwGetWindowAttrib(m_window, GLFW_CONTEXT_VERSION_MINOR);
        int v_revision = glfwGetWindowAttrib(m_window, GLFW_CONTEXT_REVISION);
        int gl_profile = glfwGetWindowAttrib(m_window, GLFW_OPENGL_PROFILE);
        int gl_forward_compat = glfwGetWindowAttrib(m_window, GLFW_OPENGL_FORWARD_COMPAT);

        std::string client_api_s = "GLFW_NO_API";
        if (client_api == GLFW_OPENGL_API) client_api_s = "GLFW_OPENGL_API";
        if (client_api == GLFW_OPENGL_ES_API) client_api_s = "GLFW_OPENGL_ES_API";

        std::string creation_api_s = "GLFW_NATIVE_CONTEXT_API";
        if (creation_api == GLFW_EGL_CONTEXT_API) creation_api_s = "GLFW_EGL_CONTEXT_API";
        if (creation_api == GLFW_OSMESA_CONTEXT_API) creation_api_s = "GLFW_OSMESA_CONTEXT_API";

        std::string opengl_profile_s = "GLFW_OPENGL_CORE_PROFILE";
        if (gl_profile == GLFW_OPENGL_COMPAT_PROFILE)
            opengl_profile_s = "GLFW_OPENGL_COMPAT_PROFILE";
        if (gl_profile == GLFW_OPENGL_ANY_PROFILE) opengl_profile_s = "GLFW_OPENGL_ANY_PROFILE";

        logger().info("Client API : {}", client_api_s);
        logger().info("Creation API : {}", creation_api_s);
        logger().info(
            "Context version | Major: {}, Minor: {}, Revision: {}",
            v_major,
            v_minor,
            v_revision);
        logger().info("Forward Compatibility: {}", (gl_forward_compat ? "True" : "False"));
        logger().info("OpenGL Profile: {}", opengl_profile_s);

        logger().info("Window scale : {}, {}", xscale, yscale);
    }


    logger().info("OpenGL Driver");
    logger().info("Vendor: {}", glGetString(GL_VENDOR));
    logger().info("Renderer: {}", glGetString(GL_RENDERER));
    logger().info("Version: {}", glGetString(GL_VERSION));
    logger().info("Shading language version: {}", glGetString(GL_SHADING_LANGUAGE_VERSION));

    /**
     * Set up callbacks
     */

    // Set custom pointer to (this) so we can retrieve it later in
    // stateless lambdas.
    glfwSetWindowUserPointer(m_window, this);

    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, int w, int h) {
        static_cast<Viewer*>(glfwGetWindowUserPointer(window))->resize(w, h);
    });

    glfwSetWindowPosCallback(m_window, [](GLFWwindow* window, int x, int y) {
        static_cast<Viewer*>(glfwGetWindowUserPointer(window))->resize(x, y);
    });


    glfwSetDropCallback(m_window, [](GLFWwindow* window, int cnt, const char** paths) {
        static_cast<Viewer*>(glfwGetWindowUserPointer(window))->drop(cnt, paths);
    });

    glfwSetCursorPosCallback(m_window, [](GLFWwindow* window, double x, double y) {
        const Eigen::Vector2f new_pos = Eigen::Vector2f(x, y);

        auto& input = static_cast<Viewer*>(glfwGetWindowUserPointer(window))->get_input();
        input.mouse_delta += new_pos - input.mouse_position;
        input.mouse_position = new_pos;
    });

    glfwSetKeyCallback(
        m_window,
        [](GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
            static_cast<Viewer*>(glfwGetWindowUserPointer(window))->m_key_queue.push({key, action});
        });

    glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button, int action, int) {
        static_cast<Viewer*>(glfwGetWindowUserPointer(window))
            ->m_mouse_key_queue.push({button, action});
    });

    glfwSetScrollCallback(m_window, [](GLFWwindow* window, double x, double y) {
        auto& input = static_cast<Viewer*>(glfwGetWindowUserPointer(window))->get_input();
        input.mouse_wheel = float(y);
        input.mouse_wheel_horizontal = float(x);
    });


    return true;
}

bool Viewer::init_imgui()
{
    IMGUI_CHECKVERSION();
    m_imgui_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imgui_context);
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init(GLState::get_glsl_version_string().c_str());

    ImGuiIO& io = ImGui::GetIO();
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; experimental
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::Spectrum::StyleColorsSpectrum();


    // Set default color picker options
    ImGui::SetColorEditOptions(
        ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB |
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueWheel);

    ImGui::GetIO().IniFilename = get_imgui_config_path().c_str();

    init_imgui_fonts();

    return true;
}

bool Viewer::init_imgui_fonts()
{
    const float base_size = 32.0f;
    ImGuiIO& io = ImGui::GetIO();

    io.Fonts->Clear();


    ImGui::Spectrum::LoadFont(base_size);

    static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;

    auto font_awesome = io.Fonts->AddFontFromMemoryCompressedTTF(
        fontawesome5_compressed_data,
        fontawesome5_compressed_size,
        base_size,
        &icons_config,
        icons_ranges);

    io.Fonts->Build();

    return font_awesome != nullptr;
}


void Viewer::resize(int window_width, int window_height)
{
    if (window_width < 0 || window_height < 0) {
        return;
    }

    int fwidth, fheight, wwidth, wheight;
    glfwGetFramebufferSize(m_window, &fwidth, &fheight);
    glfwGetWindowSize(m_window, &wwidth, &wheight);

    update_scale();


    registry().set<WindowSize>(WindowSize{window_width, window_height});

    m_width = window_width;
    m_height = window_height;

    ui::publish<WindowResizeEvent>(m_registry, window_width, window_height);
}

void Viewer::move(int x, int y)
{
    (void)(x);
    (void)(y);
    update_scale();
}

void Viewer::update_scale()
{
    Eigen::Vector2f content_scale = Eigen::Vector2f::Ones();
    glfwGetWindowContentScale(m_window, &content_scale.x(), &content_scale.y());

    int fwidth, fheight, wwidth, wheight;
    glfwGetFramebufferSize(m_window, &fwidth, &fheight);
    glfwGetWindowSize(m_window, &wwidth, &wheight);
    if (wwidth <= 0) return;
    m_ui_scaling = (wwidth / float(fwidth)) * content_scale.x();
}

void Viewer::drop(int count, const char** paths)
{
    publish<WindowDropEvent>(m_registry, count, paths);
}

std::string Viewer::get_config_folder()
{
#if defined(_WIN32)
    const char* appdata = getenv("LOCALAPPDATA");
    return std::string(appdata) + "\\lagrange\\";
#elif defined(__APPLE__)
    return std::string("~/Library/Preferences/lagrange/");
#else
    return std::string("~/.lagrange/");
#endif
}


void Viewer::process_input()
{
    // Reset input before polling events
    get_input().mouse_wheel = 0.0f;
    get_input().mouse_wheel_horizontal = 0.0f;
    get_input().mouse_delta = Eigen::Vector2f(0, 0);

    glfwPollEvents();


    /*
        Process one keyboard event at a time
    */
    if (!m_key_queue.empty()) {
        auto event = m_key_queue.front();
        get_keybinds().set_key_state(event.first, event.second);
        m_key_queue.pop();
    }

    /*
        Process one mouse key event at a time
    */
    if (!m_mouse_key_queue.empty()) {
        auto event = m_mouse_key_queue.front();
        get_keybinds().set_key_state(event.first, event.second);
        m_mouse_key_queue.pop();
    }

    // Set keybind context
    std::string keybind_context = "global";

    // If any viewport hovered, set context to "viewport"
    bool any_viewport_hovered = false;
    registry().view<const ViewportPanel>().each(
        [&](const ViewportPanel& v) { any_viewport_hovered |= v.hovered; });
    if (any_viewport_hovered) {
        keybind_context = "viewport";
    }

    get_input().keybinds->update(keybind_context);
}

void Viewer::update_time()
{
    float dt = ImGui::GetIO().DeltaTime;
    auto& global_time = registry().ctx_or_set<GlobalTime>();
    global_time.t += dt;
    global_time.dt = dt;
}

void Viewer::start_imgui_frame()
{
    // Start new ImGui Frame
    ImGui::SetCurrentContext(m_imgui_context);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();


    // Set up ui scaling
    {
        auto& io = ImGui::GetIO();
        io.FontGlobalScale = 0.5f * m_ui_scaling; // divide by two since we're oversampling
        auto& style = ImGui::GetStyle();

        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(style.FramePadding.x * m_ui_scaling, style.FramePadding.y * m_ui_scaling));

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.FrameRounding * m_ui_scaling);
        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, style.TabRounding * m_ui_scaling);
        ImGui::PushStyleVar(
            ImGuiStyleVar_ScrollbarSize,
            std::max(7.0f, style.ScrollbarSize * m_ui_scaling));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ScrollbarRounding,
            style.ScrollbarRounding * m_ui_scaling);
    }

    // Imgui Tab styling
    {
        ImGui::PushStyleColor(ImGuiCol_Tab, ImColor(255, 255, 255, 255).Value);
        ImGui::PushStyleColor(ImGuiCol_TabActive, ImColor(255, 255, 255, 255).Value);
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImColor(255, 255, 255, 255).Value);
        ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImColor(ImGui::Spectrum::GRAY200).Value);
        ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImColor(ImGui::Spectrum::GRAY400).Value);
    }
}

void Viewer::end_imgui_frame()
{
    ImGui::PopStyleColor(5);

    ImGui::PopStyleVar(5);
    ImGui::Render(); // note: this renders to imgui's vertex buffers, not to screen
}

void Viewer::make_current()
{
    assert(m_window);
    glfwMakeContextCurrent(m_window);
}


std::future<void> Viewer::run_on_main_thread(std::function<void(void)> fn)
{
    std::promise<void> fn_promise;
    auto future = fn_promise.get_future();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_main_thread_fn.push({std::move(fn_promise), std::move(fn)});
    }
    return future;
}

void Viewer::draw_menu()
{
    auto& r = registry();
    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("File")) {
        //lagrange::logger().info("TODO load/save menu");

        if (ImGui::MenuItem(ICON_FA_FILE " Clear Scene")) {
            ui::clear_scene(r);
        }

        ImGui::Separator();
#ifdef LAGRANGE_WITH_ASSIMP
        if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Import Scene")) {
            auto path = load_dialog("");
            if (!path.empty()) {
                ui::load_scene<TriangleMesh3Df>(r, path);
                ui::camera_focus_and_fit(r, get_focused_camera_entity(r));
            }
        }
#else
        ImGui::MenuItem(ICON_FA_WINDOW_CLOSE " Load Scene", nullptr, false, false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Load Scene is only available when Lagrange is compiled with\n"
                              "Assimp support (CMake option: LAGRANGE_WITH_ASSIMP=ON)");
        }
#endif

        if (ImGui::MenuItem(ICON_FA_FILE_IMPORT " Import Single Mesh")) {
            auto path = load_dialog("");
            if (!path.empty()) {
                auto m = ui::load_obj<TriangleMesh3Df>(r, path);
                if (m != NullEntity) {
                    ui::show_mesh(r, m);
                    ui::camera_focus_and_fit(r, get_focused_camera_entity(r));
                }
            }
        }

        // TODO: Add export capabilities once the UI can do useful stuff (mesh cleanup, etc).

        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_WINDOW_CLOSE " Quit")) {
            glfwSetWindowShouldClose(m_window, GL_TRUE);
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        r.view<UIPanel>().each([](UIPanel& window) {
            if (window.title.length() > 0 && window.title[0] != '#') {
                if (ImGui::MenuItem(window.title.c_str(), nullptr, window.visible)) {
                    window.visible = !window.visible;
                }
            }
        });


        ImGui::Separator();
        ImGui::MenuItem("ImGui Demo Window", "", &m_show_imgui_demo);
        ImGui::MenuItem("Style Editor", "", &m_show_imgui_style);

        ImGui::Separator();


        if (ImGui::MenuItem("New Viewport")) {
            auto viewport = add_viewport(registry(), get_focused_camera_entity(registry()));
            add_viewport_panel(registry(), "Viewport", viewport);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reset layout")) {
            // Empty the .ini file
            {
                std::ofstream f(get_imgui_config_path(), std::ios::app);
            }

            reset_layout(registry());
        }

        if (ImGui::MenuItem("Show Tab Bars")) {
            r.view<UIPanel>().each([](UIPanel& window) {
                if (window.imgui_window && window.imgui_window->DockNode) {
                    window.imgui_window->DockNode->LocalFlags &= ~ImGuiDockNodeFlags_NoTabBar;
                }
            });
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Lights")) {
        if (ImGui::MenuItem(ICON_FA_SUN " Add Directional Light")) {
            ui::add_directional_light(registry());
        }
        if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Add Point Light")) {
            ui::add_point_light(registry());
        }
        if (ImGui::MenuItem(ICON_FA_CROSSHAIRS " Add Spot Light")) {
            ui::add_spot_light(registry());
        }

         if (ImGui::MenuItem("Clear Lights")) {
            ui::clear_lights(registry());
        }

        ImGui::Separator();

        if (ImGui::MenuItem(ICON_FA_IMAGE" Load Image Based Light")) {
            auto path = ui::load_dialog("");
            if (!path.empty()){
                try {
                    auto ibl = ui::generate_ibl(path);
                    ui::clear_ibl(registry());
                    ui::add_ibl(registry(), std::move(ibl));
                } catch (const std::exception& ex) {
                    lagrange::logger().error("Failed to load IBL: {}", ex.what());
                }
            }
        }

        if (ImGui::MenuItem("Set White Background")) {
            ui::clear_ibl(registry());
            ui::add_ibl(
                registry(),
                ui::generate_ibl(
                    ui::generate_colormap([](float v) { return Color::white(); }),
                    16));
        }

        if (ImGui::MenuItem("Set Black Background")) {
            ui::clear_ibl(registry());
            ui::add_ibl(
                registry(),
                ui::generate_ibl(
                    ui::generate_colormap([](float v) { return Color::black(); }),
                    16));
        }

        if (ImGui::MenuItem("Clear IBL")) {
            ui::clear_ibl(registry());
        }

        ImGui::EndMenu();

    }

    {
        auto view = r.view<UIPanel>();
        for (auto e : view) {
            auto& window = view.get<UIPanel>(e);
            if (window.menubar_fn) window.menubar_fn(r, e);
        }
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetFontSize() * 5);
    ImGui::Text("(%.0f fps)", ImGui::GetIO().Framerate);
    if ((m_initial_window_options.fullscreen || m_initial_window_options.window_fullscreen)) {
        ImGui::PushID("x");
        if (ImGui::Button(ICON_FA_TIMES)) {
            glfwSetWindowShouldClose(m_window, GL_TRUE);
        }
        ImGui::PopID();
    }

    // Update context to have main menu height - needed for toolbar rendering
    r.set<MainMenuHeight>(MainMenuHeight{ImGui::GetWindowSize().y});

    ImGui::EndMainMenuBar();
}

void Viewer::start_dockspace()
{
    // Set dockspace from Imgui to context
    auto dockspace = registry().set<Dockspace>(Dockspace{ImGui::GetID("MyDockSpace")});

    if (ImGui::DockBuilderGetNode(dockspace.ID) == NULL) {
        reset_layout(registry());
    }

    // Position dockspace window
    {
        float left_offset = 0.0f;

        const auto& toolbar = m_registry.get<UIPanel>(m_registry.ctx<DefaultPanels>().toolbar);
        if (toolbar.visible) {
            left_offset += ToolbarPanel::toolbar_width;
        }

        const auto menubar_height = get_menu_height(registry()).height;
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(left_offset, menubar_height));
        ImGui::SetNextWindowSize(
            ImVec2(viewport->Size.x - left_offset, viewport->Size.y - menubar_height));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    }

    ImGui::Begin(
        "Dockspace window",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);

    {
        ImGui::PopStyleVar(3);
    }


    ImGui::DockSpace(dockspace.ID);
}

void Viewer::end_dockspace()
{
    ImGui::End();
}

void Viewer::show_last_shader_error()
{
    if (ImGui::BeginPopupModal(MODAL_NAME_SHADER_ERROR)) {
        ImGui::Text("%s", m_last_shader_error_desc.c_str());

        ImGui::InputTextMultiline(
            "",
            &m_last_shader_error,
            ImVec2(float((get_width() / 3) * 2), float((get_height() / 5) * 4)));

        if (m_last_shader_error.length() == 0) {
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Try again", ImVec2(ImGui::GetContentRegionAvail().x, 40.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}


bool Viewer::m_instance_initialized = false;

} // namespace ui
} // namespace lagrange
