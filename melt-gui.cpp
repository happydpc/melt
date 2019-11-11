#define _USE_MATH_DEFINES
#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "imgui.cpp"
#include "imgui_draw.cpp"
#include "imgui_impl_glfw_gl3.cpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "minitrace.h"

#include "generated/bunny_obj.h"
#include "generated/column_obj.h"
#include "generated/cube_obj.h"
#include "generated/sphere_obj.h"
#include "generated/teapot_obj.h"
#include "generated/suzanne_obj.h"

#include <cassert>

//#define MELT_DEBUG
#define MELT_ASSERT(stmt) assert(stmt)
#define MELT_PROFILE_BEGIN() MTR_BEGIN("MELT", __func__)
#define MELT_PROFILE_END() MTR_END("MELT", __func__)
#define MELT_IMPLEMENTATION
#include "melt.h"

struct ModelMesh
{
    ModelMesh()
    {
        std::memset(this, 0x0, sizeof(ModelMesh));
    }
    GLuint Program;
    struct
    {
        GLuint vao;
        GLuint vbo;
    } MeshBuffer;
    struct
    {
        GLuint vao;
        GLuint vbo;
        GLuint indices;
    } OccluderBuffer;
    GLint ModelViewProjection;
    GLint Alpha;
    int VertexCount;
    MeltMesh InputMesh;
};

struct Camera
{
    glm::mat4 view;
    glm::vec3 position;
};

struct ScopedTimer
{
    ScopedTimer()
    {
        start = std::chrono::high_resolution_clock::now();
    }
    ~ScopedTimer()
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto timing = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        printf("Total time: %lldus\n", timing);
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
};

static bool LoadModelMesh(const char* model_path, MeltParams& melt_params, ModelMesh& out_model_mesh, std::vector<glm::vec3>& out_buffer_data)
{
    GLchar* vertex_source = (GLchar*)R"END(
        #version 150
        in vec3 position;
        in vec3 color;
        uniform mat4 ModelViewProjection;
        out vec3 f_normal;
        out vec3 f_color;
        void main(void) {
            gl_Position = ModelViewProjection * vec4(position, 1.0);
            f_color = color;
        }
    )END";

    GLchar* fragment_source = (GLchar*)R"END(
        #version 150
        in vec3 f_color;
        out vec4 color;
        uniform float alpha;
        void main(void) {
            color = vec4(f_color, alpha);
        }
    )END";

    GLint is_compiled, is_linked;

    GLuint vertex_id = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_id, 1, &vertex_source, NULL);
    glCompileShader(vertex_id);
    glGetShaderiv(vertex_id, GL_COMPILE_STATUS, &is_compiled);
    assert(is_compiled && "Vertex shader complation failed");

    GLuint fragment_id = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_id, 1, &fragment_source, NULL);
    glCompileShader(fragment_id);
    glGetShaderiv(fragment_id, GL_COMPILE_STATUS, &is_compiled);
    assert(is_compiled && "Fragment shader complation failed");

    out_model_mesh.Program = glCreateProgram();

    glAttachShader(out_model_mesh.Program, vertex_id);
    glAttachShader(out_model_mesh.Program, fragment_id);

    glBindAttribLocation(out_model_mesh.Program, 0, "position");
    glBindAttribLocation(out_model_mesh.Program, 1, "color");

    glLinkProgram(out_model_mesh.Program);
    glGetProgramiv(out_model_mesh.Program, GL_LINK_STATUS, &is_linked);
    assert(is_linked && "Program link failed");

    glDeleteShader(vertex_id);
    glDeleteShader(fragment_id);

    out_model_mesh.ModelViewProjection = glGetUniformLocation(out_model_mesh.Program, "ModelViewProjection");
    out_model_mesh.Alpha = glGetUniformLocation(out_model_mesh.Program, "alpha");

    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string error;
    bool obj_parsing_res;

    const char* model = nullptr;

    if (strcmp(model_path, "bunny.obj") == 0)
        model = s_bunny_obj;
    else if (strcmp(model_path, "column.obj") == 0)
        model = s_column_obj;
    else if (strcmp(model_path, "cube.obj") == 0)
        model = s_cube_obj;
    else if (strcmp(model_path, "sphere.obj") == 0)
        model = s_sphere_obj;
    else if (strcmp(model_path, "suzanne.obj") == 0)
        model = s_suzanne_obj;
    else if (strcmp(model_path, "teapot.obj") == 0)
        model = s_teapot_obj;

    if (model != nullptr)
    {
        std::stringstream obj_stream(model);
        tinyobj::MaterialFileReader material_reader("");
        obj_parsing_res = tinyobj::LoadObj(shapes, materials, error, obj_stream, material_reader);
    }
    else
    {
        obj_parsing_res = tinyobj::LoadObj(shapes, materials, error, model_path, NULL);    
    }    

    if (!error.empty()) 
    {
        fprintf(stderr, "%s\n", error.c_str());
        return false;
    }

    if (!obj_parsing_res)
        return false;    

    out_model_mesh.VertexCount = 0;
    for (size_t i = 0; i < shapes.size(); i++) 
        out_model_mesh.VertexCount += shapes[i].mesh.indices.size();

    int output_index = 0;
    out_buffer_data.resize(out_model_mesh.VertexCount * 2);
    for (size_t i = 0; i < shapes.size(); i++) 
    {
        for (size_t f = 0; f < shapes[i].mesh.indices.size(); f++) 
        {
            auto position = &shapes[i].mesh.positions[shapes[i].mesh.indices[f] * 3];
            out_buffer_data[output_index++] = *reinterpret_cast<glm::vec3*>(position);
            out_buffer_data[output_index++] = glm::vec3(1.0f, 0.5f, 0.5f);
        }
    }
    
    melt_params.mesh.vertices.clear();
    melt_params.mesh.indices.clear();
    for (size_t i = 0; i < shapes.size(); i++) 
    {
        for (size_t f = 0; f < shapes[i].mesh.indices.size(); f++) 
            melt_params.mesh.indices.push_back(shapes[i].mesh.indices[f]);

        for (size_t v = 0; v < shapes[i].mesh.positions.size() / 3; v++) 
        {
            melt_params.mesh.vertices.emplace_back();
            melt_params.mesh.vertices.back().x = shapes[i].mesh.positions[3 * v + 0];
            melt_params.mesh.vertices.back().y = shapes[i].mesh.positions[3 * v + 1];
            melt_params.mesh.vertices.back().z = shapes[i].mesh.positions[3 * v + 2];
        }
    }

    return true;
}

static Camera FPSCameraViewMatrix(GLFWwindow* window, bool ignore_input)
{
    static glm::vec3 position(-4.0f, 0.0f, 0.0f);
    static float rotation[] = { 0.0f, 0.0f };
    static double lastMouse[] = { 0.0, 0.0 };
    double mouse[2];

    glfwGetCursorPos(window, &mouse[0], &mouse[1]);
    if (!ignore_input && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
    {
        rotation[0] += (float)(mouse[1] - lastMouse[1]) * 0.005f;
        rotation[1] += (float)(mouse[0] - lastMouse[0]) * 0.005f;
    }
    lastMouse[0] = mouse[0];
    lastMouse[1] = mouse[1];

    float pitch = -rotation[0];
    float yaw = rotation[1];

    if (pitch >= (float) M_PI_2) pitch = (float) M_PI_2;
    if (pitch <= (float)-M_PI_2) pitch = (float)-M_PI_2;

    glm::vec3 forward;
    const glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
    forward.x = cos(pitch) * cos(yaw);
    forward.y = sin(pitch);
    forward.z = cos(pitch) * sin(yaw);
    forward = glm::normalize(forward);

    float speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 0.1f : 0.01f;
    if (!ignore_input)
    {
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            position -= forward * speed;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            position += forward * speed;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            position -= glm::normalize(glm::cross(forward, up)) * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            position += glm::normalize(glm::cross(forward, up)) * speed;
    }

    glm::mat4 view = glm::lookAt(position, position + forward, up);

    return {view, position};
}

static void ErrorCallback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

static void SetupIMGUIStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowMinSize     = ImVec2(320, 5000);
    style.FramePadding      = ImVec2(6, 6);
    style.ItemSpacing       = ImVec2(6, 6);
    style.ItemInnerSpacing  = ImVec2(6, 6);
    style.Alpha             = 1.0f;
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 0.0f;
    style.IndentSpacing     = 6.0f;
    style.ItemInnerSpacing  = ImVec2(6, 6);
    style.ColumnsMinSpacing = 50.0f;
    style.GrabMinSize       = 14.0f;
    style.GrabRounding      = 0.0f;
    style.ScrollbarSize     = 12.0f;
    style.ScrollbarRounding = 0.0f;

    style.Colors[ImGuiCol_Text]                  = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled]          = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_ChildWindowBg]         = ImVec4(0.20f, 0.20f, 0.20f, 0.58f);
    style.Colors[ImGuiCol_Border]                = ImVec4(0.31f, 0.31f, 0.31f, 0.00f);
    style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.20f, 0.20f, 0.20f, 0.60f);
    style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
    style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.20f, 0.22f, 0.27f, 0.75f);
    style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.20f, 0.22f, 0.27f, 0.47f);
    style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.47f, 0.47f, 0.47f, 0.21f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_ComboBg]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_CheckMark]             = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab]            = ImVec4(0.47f, 0.47f, 0.47f, 0.14f);
    style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_Button]                = ImVec4(0.47f, 0.47f, 0.47f, 0.14f);
    style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
    style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_Header]                = ImVec4(0.92f, 0.18f, 0.29f, 0.76f);
    style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
    style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_Column]                = ImVec4(0.47f, 0.77f, 0.83f, 0.32f);
    style.Colors[ImGuiCol_ColumnHovered]         = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
    style.Colors[ImGuiCol_ColumnActive]          = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.47f, 0.77f, 0.83f, 0.04f);
    style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
    style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_CloseButton]           = ImVec4(0.86f, 0.93f, 0.89f, 0.16f);
    style.Colors[ImGuiCol_CloseButtonHovered]    = ImVec4(0.86f, 0.93f, 0.89f, 0.39f);
    style.Colors[ImGuiCol_CloseButtonActive]     = ImVec4(0.86f, 0.93f, 0.89f, 1.00f);
    style.Colors[ImGuiCol_PlotLines]             = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
    style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(0.86f, 0.86f, 0.86f, 0.63f);
    style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.92f, 0.18f, 0.29f, 0.43f);
    style.Colors[ImGuiCol_ModalWindowDarkening]  = ImVec4(0.20f, 0.22f, 0.27f, 0.73f);
    style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
    ImGuiIO& io = ImGui::GetIO();

#if __APPLE__
    io.Fonts->AddFontFromFileTTF("/Library/Fonts/Arial Narrow Bold.ttf", 16);
#endif
}

static bool ComputeMeshConservativeOcclusion(const char* mesh_path, MeltParams& melt_params, MeltResult& melt_result, ModelMesh& out_mesh)
{
    if (out_mesh.MeshBuffer.vao)
        glDeleteVertexArrays(1, &out_mesh.MeshBuffer.vao);
    if (out_mesh.MeshBuffer.vbo)
        glDeleteBuffers(1, &out_mesh.MeshBuffer.vbo);
    if (out_mesh.OccluderBuffer.vao)
        glDeleteVertexArrays(1, &out_mesh.OccluderBuffer.vao);
    if (out_mesh.OccluderBuffer.vbo)
        glDeleteBuffers(1, &out_mesh.OccluderBuffer.vbo);
    if (out_mesh.OccluderBuffer.indices)
        glDeleteBuffers(1, &out_mesh.OccluderBuffer.indices);
    if (out_mesh.Program)
        glDeleteProgram(out_mesh.Program);

    std::vector<glm::vec3> buffer_data;
    bool model_loaded = LoadModelMesh(mesh_path, melt_params, out_mesh, buffer_data);
    if (!model_loaded)
        return false;
    
    glGenVertexArrays(1, &out_mesh.MeshBuffer.vao);
    glBindVertexArray(out_mesh.MeshBuffer.vao);
    glGenBuffers(1, &out_mesh.MeshBuffer.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, out_mesh.MeshBuffer.vbo);

    glBufferData(GL_ARRAY_BUFFER, out_mesh.VertexCount * sizeof(glm::vec3) * 2, buffer_data.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 3));

    {
        ScopedTimer scoped_timer;
        MeltGenerateOccluder(melt_params, melt_result);
    }

    glGenVertexArrays(1, &out_mesh.OccluderBuffer.vao);
    glBindVertexArray(out_mesh.OccluderBuffer.vao);
    
    glGenBuffers(1, &out_mesh.OccluderBuffer.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, out_mesh.OccluderBuffer.vbo);
    glBufferData(GL_ARRAY_BUFFER, melt_result.debugMesh.vertices.size() * sizeof(glm::vec3), melt_result.debugMesh.vertices.data(), GL_STATIC_DRAW);
    
    glGenBuffers(1, &out_mesh.OccluderBuffer.indices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out_mesh.OccluderBuffer.indices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, melt_result.debugMesh.indices.size() * sizeof(GLushort), melt_result.debugMesh.indices.data(), GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 3));
    
    return true;
}

int main(int argc, char* argv[])
{
    glfwSetErrorCallback(ErrorCallback);

    if (!glfwInit())
    {
        fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    mtr_init("trace.json");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(920, 720, "", NULL, NULL);
    assert(window && "Window creation failed");
    
    struct OcclusionContext
    {
        MeltParams* melt_params;
        MeltResult* melt_result;
        ModelMesh* model_mesh;
    };

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);

    ImGui_ImplGlfwGL3_Init(window, true);
    
    MeltParams melt_params;
    melt_params.debug.voxelScale = 0.8f;
    melt_params.voxelSize = 0.25f;
    melt_params.fillPercentage = 1.0f;
    
    MeltResult melt_result;
    ModelMesh model_mesh;

    if (argc >= 2)
        ComputeMeshConservativeOcclusion(argv[1], melt_params, melt_result, model_mesh);
    
    OcclusionContext occlusion_context;
    occlusion_context.melt_params = &melt_params;
    occlusion_context.melt_result = &melt_result;
    occlusion_context.model_mesh = &model_mesh;
    glfwSetWindowUserPointer(window, &occlusion_context);
    glfwSetDropCallback(window, [](GLFWwindow* window, int count, const char** paths)
    {
        const OcclusionContext& occlusion_context = *(OcclusionContext*)glfwGetWindowUserPointer(window);

        ComputeMeshConservativeOcclusion(paths[0], 
            *occlusion_context.melt_params, 
            *occlusion_context.melt_result, 
            *occlusion_context.model_mesh);
    });

    SetupIMGUIStyle();

    glClearColor(0.64f, 0.76f, 0.91f , 1.0f);
    glDepthFunc(GL_LEQUAL);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        ImGui_ImplGlfwGL3_NewFrame();

        bool generate_clicked = false;
        bool model_selected = false;
        static float alpha = 0.25f;
        static bool depth_test = false;
        static bool show_slice_selection = false;
        static bool show_inner = true;
        static bool show_outer = true;
        static bool show_dist = false;
        static bool show_extent = false;
        static bool show_result = true;
        static bool show_debug_gui = false;
        static bool box_type_diagonals = false;
        static bool box_type_top = false;
        static bool box_type_bottom = false;
        static bool box_type_sides = false;
        static bool box_type_regular = true;
        static const char* obj_models[] = 
        { 
            "bunny.obj", 
            "column.obj", 
            "cube.obj",
            "sphere.obj",
            "suzanne.obj",
            "teapot.obj",
        };
        static int obj_model_index = 0;
        {
            melt_params.debug.flags = 0;

            melt_params.boxTypeFlags = 0;

            ImGui::SetNextWindowPos(ImVec2(0, 0));

            ImGuiWindowFlags options =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings;

            ImGui::Begin("Fixed Overlay", nullptr, ImVec2(0,0), 0.3f, options);

            ImGui::Text("Drag and drop an .obj model");
            
            if (ImGui::Combo("Obj model", &obj_model_index, obj_models, IM_ARRAYSIZE(obj_models)))
            {
                model_selected = true;
            }

            ImGui::Checkbox("Show Debug Controls", &show_debug_gui);
            ImGui::InputFloat("Voxel Size", &melt_params.voxelSize);
            ImGui::DragFloat("Fill Percentage", &melt_params.fillPercentage, 0.01f, 0.0f, 1.0f);

            ImGui::Checkbox("BoxTypeDiagonals", &box_type_diagonals);
            ImGui::Checkbox("BoxTypeTop", &box_type_top);
            ImGui::Checkbox("BoxTypeBottom", &box_type_bottom);
            ImGui::Checkbox("BoxTypeSides", &box_type_sides);
            ImGui::Checkbox("BoxTypeRegular", &box_type_regular);

            if (box_type_diagonals) melt_params.boxTypeFlags |= MeltOccluderBoxTypeDiagonals;
            if (box_type_top) melt_params.boxTypeFlags |= MeltOccluderBoxTypeTop;
            if (box_type_bottom) melt_params.boxTypeFlags |= MeltOccluderBoxTypeBottom;
            if (box_type_sides) melt_params.boxTypeFlags |= MeltOccluderBoxTypeSides;
            if (box_type_regular) melt_params.boxTypeFlags = MeltOccluderBoxTypeRegular;

            if (ImGui::Button("Generate"))
            {
                generate_clicked = true;
            }

            if (show_debug_gui)
            {
                ImGui::InputFloat("Voxel Scale", &melt_params.debug.voxelScale);
                ImGui::DragFloat("Alpha", &alpha, 0.01f, 0.0f, 1.0f);

                ImGui::InputInt("Slice X", &melt_params.debug.sliceIndexX);
                ImGui::InputInt("Slice Y", &melt_params.debug.sliceIndexY);
                ImGui::InputInt("Slice Z", &melt_params.debug.sliceIndexZ);
                ImGui::InputInt("Voxel X", &melt_params.debug.voxelX);
                ImGui::InputInt("Voxel Y", &melt_params.debug.voxelY);
                ImGui::InputInt("Voxel Z", &melt_params.debug.voxelZ);
                ImGui::InputInt("Extent Index", &melt_params.debug.extentIndex);
                ImGui::InputInt("Extent Max Step", &melt_params.debug.extentMaxStep);
                ImGui::Checkbox("Show Slice Selection", &show_slice_selection);
                ImGui::Checkbox("Show Inner", &show_inner);
                ImGui::Checkbox("Show Outer", &show_outer);
                ImGui::Checkbox("Show Dist", &show_dist);
                ImGui::Checkbox("Show Extent", &show_extent);
                ImGui::Checkbox("Show Result", &show_result);

                if (show_inner) melt_params.debug.flags |= MeltDebugTypeShowInner;
                if (show_slice_selection) melt_params.debug.flags |= MeltDebugTypeShowSliceSelection;
                if (show_outer) melt_params.debug.flags |= MeltDebugTypeShowOuter;
                if (show_dist) melt_params.debug.flags |= MeltDebugTypeShowMinDistance;
                if (show_extent) melt_params.debug.flags |= MeltDebugTypeShowExtent;
                if (show_result) melt_params.debug.flags |= MeltDebugTypeShowResult;

                ImGui::Checkbox("Depth Test", &depth_test);
                if (ImGui::Button("Next Diagonal"))
                {
                    melt_params.debug.voxelX++;
                    melt_params.debug.voxelY++;
                    melt_params.debug.voxelZ++;
                }
                if (ImGui::Button("Previous Diagonal"))
                {
                    melt_params.debug.voxelX--;
                    melt_params.debug.voxelY--;
                    melt_params.debug.voxelZ--;
                }
            }
            else
            {
                melt_params.debug.extentIndex = -1;
                melt_params.debug.flags |= MeltDebugTypeShowResult;
            }
            ImGui::End();
        }

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);

        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        {
            if (depth_test)
            {
                glDisable(GL_BLEND);
                glEnable(GL_DEPTH_TEST);
            }
            else
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_DEPTH_TEST);
            }

            Camera camera = FPSCameraViewMatrix(window, ImGui::IsAnyItemActive());

            float fov = 55.0f;
            glm::mat4 projection = glm::perspective(glm::radians(fov), float(width) / height, 0.01f, 100.0f);
            glm::mat4 viewProjection = projection * camera.view;

            if (model_mesh.Program)
                glUseProgram(model_mesh.Program);

            glUniformMatrix4fv(model_mesh.ModelViewProjection, 1, GL_FALSE, glm::value_ptr(viewProjection));
            glUniform1f(model_mesh.Alpha, alpha);

            if (model_mesh.MeshBuffer.vao && model_mesh.VertexCount > 0)
            {
                glBindVertexArray(model_mesh.MeshBuffer.vao);
                glDrawArrays(GL_TRIANGLES, 0, model_mesh.VertexCount);
            }
            if (model_mesh.OccluderBuffer.vao && melt_result.debugMesh.indices.size() > 0)
            {
                glBindVertexArray(model_mesh.OccluderBuffer.vao);
                glDrawElements(GL_TRIANGLES, melt_result.debugMesh.indices.size(), GL_UNSIGNED_SHORT, 0);
            }
        }

        if (generate_clicked)
        {
            {
                ScopedTimer scoped_timer;
                MeltGenerateOccluder(melt_params, melt_result);
            }

            glBindBuffer(GL_ARRAY_BUFFER, model_mesh.OccluderBuffer.vbo);
            glBufferData(GL_ARRAY_BUFFER, melt_result.debugMesh.vertices.size() * sizeof(glm::vec3), melt_result.debugMesh.vertices.data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model_mesh.OccluderBuffer.indices);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, melt_result.debugMesh.indices.size() * sizeof(GLushort), melt_result.debugMesh.indices.data(), GL_STATIC_DRAW);
        }
        if (model_selected)
        {
            ComputeMeshConservativeOcclusion(obj_models[obj_model_index], melt_params, melt_result, model_mesh);
        }

        ImGui::Render();

        glfwSwapBuffers(window);
    }

    ImGui_ImplGlfwGL3_Shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    mtr_shutdown();

    return 0;
}
