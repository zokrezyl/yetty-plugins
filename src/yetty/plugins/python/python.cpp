#include "python.h"
#include "yetty_wgpu.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <spdlog/spdlog.h>

// Python must be included after other headers to avoid conflicts
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// Forward declaration of yetty_wgpu module init
extern "C" PyObject* PyInit_yetty_wgpu(void);

namespace yetty {

//-----------------------------------------------------------------------------
// Venv setup helper
//-----------------------------------------------------------------------------

static std::string getPythonPackagesPath() {
    // Use XDG_CACHE_HOME/yetty/python-packages (defaults to ~/.cache/yetty/python-packages)
    // Packages are cache-like since they can be regenerated via pip install
    const char* cacheHome = std::getenv("XDG_CACHE_HOME");
    if (cacheHome && cacheHome[0] != '\0') {
        return std::string(cacheHome) + "/yetty/python-packages";
    }
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.cache/yetty/python-packages";
}

static bool setupPythonPackages() {
    std::string pkgPath = getPythonPackagesPath();

    // Check if packages already exist (check for pygfx directory)
    if (fs::exists(pkgPath + "/pygfx")) {
        spdlog::info("Python packages ready at {}", pkgPath);
        return true;
    }

    // Create the directory
    spdlog::info("Installing pygfx and fastplotlib to {}...", pkgPath);
    fs::create_directories(pkgPath);

    // Use the embedded Python's pip (CMAKE_BINARY_DIR/python/install/bin/python3)
    // Need to set LD_LIBRARY_PATH for libpython3.13.so
    std::string pythonDir = std::string(CMAKE_BINARY_DIR) + "/python/install";
    std::string ldPath = "LD_LIBRARY_PATH=" + pythonDir + "/lib:$LD_LIBRARY_PATH ";
    std::string embeddedPip = pythonDir + "/bin/python3 -m pip";
    std::string installCmd = ldPath + embeddedPip + " install --target=" + pkgPath + " --quiet pygfx fastplotlib wgpu glfw pillow imageio 2>&1";

    spdlog::info("Running: {}", installCmd);
    if (std::system(installCmd.c_str()) != 0) {
        spdlog::error("Failed to install Python packages");
        return false;
    }

    spdlog::info("Python packages installed successfully");
    return true;
}

//-----------------------------------------------------------------------------
// PythonPlugin
//-----------------------------------------------------------------------------

PythonPlugin::~PythonPlugin() {
    (void)dispose();
}

Result<PluginPtr> PythonPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new PythonPlugin(std::move(engine)));
    if (auto res = static_cast<PythonPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init PythonPlugin", res);
    }
    return Ok(p);
}

Result<void> PythonPlugin::init() noexcept {
    // Setup packages with pygfx/fastplotlib
    if (!setupPythonPackages()) {
        spdlog::warn("Failed to setup Python packages - pygfx features may not work");
    }

    auto result = initPython();
    if (!result) {
        return result;
    }

    _initialized = true;
    spdlog::info("PythonPlugin initialized");
    return Ok();
}

Result<void> PythonPlugin::initPython() {
    if (_py_initialized) {
        return Ok();
    }

    // Set YETTY_WGPU_LIB_PATH so wgpu-py uses the same wgpu-native as yetty
    // This MUST be done before any Python/wgpu imports
    std::string wgpuLibPath = std::string(CMAKE_BINARY_DIR) + "/_deps/wgpu-native/lib/libwgpu_native.so";
    setenv("YETTY_WGPU_LIB_PATH", wgpuLibPath.c_str(), 1);
    spdlog::info("Set YETTY_WGPU_LIB_PATH={}", wgpuLibPath);

    // Register yetty_wgpu as a built-in module BEFORE Py_Initialize
    if (PyImport_AppendInittab("yetty_wgpu", PyInit_yetty_wgpu) == -1) {
        return Err<void>("Failed to register yetty_wgpu module");
    }

    // Configure Python for embedding
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);

    // Set program name
    PyStatus status = PyConfig_SetString(&config, &config.program_name, L"yetty-python");
    if (PyStatus_Exception(status)) {
        PyConfig_Clear(&config);
        return Err<void>("Failed to set Python program name");
    }

    // Import site module for full stdlib support
    config.site_import = 1;

    // Initialize Python
    status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);

    if (PyStatus_Exception(status)) {
        return Err<void>("Failed to initialize Python interpreter");
    }

    // Get main module and its dictionary
    _main_module = PyImport_AddModule("__main__");
    if (!_main_module) {
        Py_Finalize();
        return Err<void>("Failed to get Python __main__ module");
    }

    _main_dict = PyModule_GetDict(_main_module);
    if (!_main_dict) {
        Py_Finalize();
        return Err<void>("Failed to get Python __main__ dict");
    }

    _py_initialized = true;
    spdlog::info("Python {} interpreter initialized", Py_GetVersion());

    // Add packages directory to sys.path
    std::string pkgPath = getPythonPackagesPath();
    if (fs::exists(pkgPath)) {
        std::string code = "import sys; sys.path.insert(0, '" + pkgPath + "')";
        PyRun_SimpleString(code.c_str());
        spdlog::info("Added Python packages to path: {}", pkgPath);
    }

    // Also add the yetty_pygfx module path
    std::string pygfxPath = std::string(CMAKE_BINARY_DIR) + "/python";
    if (fs::exists(pygfxPath)) {
        std::string code = "import sys; sys.path.insert(0, '" + pygfxPath + "')";
        PyRun_SimpleString(code.c_str());
    }

    return Ok();
}

Result<void> PythonPlugin::dispose() {
    // Dispose layers first
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose PythonPlugin base", res);
    }

    // Cleanup yetty_wgpu resources
    yetty_wgpu_cleanup();

    // Note: We intentionally don't call Py_Finalize() here because it causes
    // segfaults when wgpu-py's resources are still being cleaned up.
    // The OS will clean up when the process exits.
    if (_py_initialized) {
        _main_module = nullptr;
        _main_dict = nullptr;
        // Py_Finalize();  // Causes segfault - skip for now
        _py_initialized = false;
        spdlog::info("Python interpreter cleanup complete");
    }

    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> PythonPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<PythonLayer>(this);
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to init PythonLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<std::string> PythonPlugin::execute(const std::string& code) {
    if (!_py_initialized) {
        return Err<std::string>("Python not initialized");
    }

    // Redirect stdout/stderr to capture output
    PyObject* sys = PyImport_ImportModule("sys");
    if (!sys) {
        return Err<std::string>("Failed to import sys module");
    }

    PyObject* io = PyImport_ImportModule("io");
    if (!io) {
        Py_DECREF(sys);
        return Err<std::string>("Failed to import io module");
    }

    // Create StringIO for capturing output
    PyObject* string_io_class = PyObject_GetAttrString(io, "StringIO");
    PyObject* string_io = PyObject_CallObject(string_io_class, nullptr);
    Py_DECREF(string_io_class);

    // Save original stdout/stderr
    PyObject* old_stdout = PyObject_GetAttrString(sys, "stdout");
    PyObject* old_stderr = PyObject_GetAttrString(sys, "stderr");

    // Redirect stdout/stderr to our StringIO
    PyObject_SetAttrString(sys, "stdout", string_io);
    PyObject_SetAttrString(sys, "stderr", string_io);

    // Execute the code
    PyObject* result = PyRun_String(code.c_str(), Py_file_input, _main_dict, _main_dict);

    // Get captured output
    PyObject* getvalue = PyObject_GetAttrString(string_io, "getvalue");
    PyObject* output_obj = PyObject_CallObject(getvalue, nullptr);
    Py_DECREF(getvalue);

    std::string output;
    if (output_obj && PyUnicode_Check(output_obj)) {
        output = PyUnicode_AsUTF8(output_obj);
    }
    Py_XDECREF(output_obj);

    // Restore stdout/stderr
    PyObject_SetAttrString(sys, "stdout", old_stdout);
    PyObject_SetAttrString(sys, "stderr", old_stderr);
    Py_DECREF(old_stdout);
    Py_DECREF(old_stderr);
    Py_DECREF(string_io);
    Py_DECREF(io);
    Py_DECREF(sys);

    if (!result) {
        // Get error info
        PyErr_Print();
        PyErr_Clear();
        return Err<std::string>("Python execution error: " + output);
    }

    Py_DECREF(result);
    return Ok(output);
}

Result<void> PythonPlugin::runFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return Err<void>("Failed to open Python file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    auto result = execute(buffer.str());
    if (!result) {
        return Err<void>("Failed to execute Python file", result);
    }

    spdlog::info("Python file executed: {}", path);
    return Ok();
}

//-----------------------------------------------------------------------------
// PythonLayer
//-----------------------------------------------------------------------------

PythonLayer::PythonLayer(PythonPlugin* plugin)
    : _plugin(plugin) {}

PythonLayer::~PythonLayer() {
    (void)dispose();
}

Result<void> PythonLayer::init(const std::string& payload) {
    _payload = payload;

    // Payload can be a Python script path or inline code
    if (!payload.empty()) {
        // Check if it's a file path
        std::ifstream test(payload);
        if (test.good()) {
            _script_path = payload;
            test.close();

            // Execute the script
            auto result = _plugin->runFile(_script_path);
            if (!result) {
                _output = "Error: " + result.error().message();
                spdlog::error("PythonLayer: failed to run script: {}", _script_path);
            } else {
                _output = "Script executed: " + _script_path;
            }
        } else {
            // Treat as inline code
            auto result = _plugin->execute(payload);
            if (result) {
                _output = *result;
            } else {
                _output = "Error: " + result.error().message();
            }
        }
    }

    _initialized = true;
    return Ok();
}

Result<void> PythonLayer::dispose() {
    // Cleanup blit resources first (before Python cleanup)
    if (_blit_bind_group) {
        wgpuBindGroupRelease(_blit_bind_group);
        _blit_bind_group = nullptr;
    }
    if (_blit_pipeline) {
        wgpuRenderPipelineRelease(_blit_pipeline);
        _blit_pipeline = nullptr;
    }
    if (_blit_sampler) {
        wgpuSamplerRelease(_blit_sampler);
        _blit_sampler = nullptr;
    }
    _blit_initialized = false;

    // Cleanup pygfx resources (only if Python is still initialized)
    if (_plugin && _plugin->isInitialized()) {
        if (_render_frame_func) {
            Py_DECREF(_render_frame_func);
            _render_frame_func = nullptr;
        }
        if (_pygfx_module) {
            // Call cleanup
            PyObject* cleanup_func = PyObject_GetAttrString(_pygfx_module, "cleanup");
            if (cleanup_func) {
                PyObject* result = PyObject_CallObject(cleanup_func, nullptr);
                Py_XDECREF(result);
                Py_DECREF(cleanup_func);
            }
            Py_DECREF(_pygfx_module);
            _pygfx_module = nullptr;
        }
    } else {
        // Python already finalized, just null out pointers
        _render_frame_func = nullptr;
        _pygfx_module = nullptr;
    }
    _pygfx_initialized = false;
    _wgpu_handles_set = false;

    _initialized = false;
    return Ok();
}

Result<void> PythonLayer::render(WebGPUContext& ctx) {
    if (_failed) return Err<void>("PythonLayer already failed");
    if (!_visible) return Ok();

    // Initialize pygfx on first render if not already done
    if (!_wgpu_handles_set) {
        // Set WebGPU handles for yetty_wgpu module
        // Note: We need instance and adapter from somewhere - for now pass nullptr
        // They can be set later if needed
        yetty_wgpu_set_handles(
            nullptr,  // instance - not exposed by WebGPUContext yet
            nullptr,  // adapter - not exposed by WebGPUContext yet
            ctx.getDevice(),
            ctx.getQueue()
        );
        _wgpu_handles_set = true;
        spdlog::info("PythonLayer: WebGPU handles set for yetty_wgpu");
    }

    // Try to get render_frame function if not already cached
    if (!_render_frame_func) {
        _pygfx_module = PyImport_ImportModule("yetty_pygfx");
        if (_pygfx_module) {
            _render_frame_func = PyObject_GetAttrString(_pygfx_module, "render_frame");
            if (_render_frame_func) {
                _pygfx_initialized = true;
                spdlog::info("PythonLayer: yetty_pygfx.render_frame cached");
            }
        }
    }

    // If pygfx is initialized, render it
    if (_pygfx_initialized && _render_frame_func) {
        bool pygfx_ok = renderPygfx();
        bool blit_ok = blitRenderTexture(ctx);
        // Log first successful frame
        static bool logged = false;
        if (!logged && pygfx_ok && blit_ok) {
            spdlog::info("PythonLayer: First frame rendered and blitted successfully");
            logged = true;
        }
    }

    return Ok();
}

bool PythonLayer::renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    (void)pass;
    (void)ctx;
    // Python layer doesn't render to pass directly
    return true;
}

bool PythonLayer::initPygfx(WebGPUContext& ctx, uint32_t width, uint32_t height) {
    if (_pygfx_initialized) {
        return true;
    }

    // Ensure WebGPU handles are set
    if (!_wgpu_handles_set) {
        yetty_wgpu_set_handles(
            nullptr,
            nullptr,
            ctx.getDevice(),
            ctx.getQueue()
        );
        _wgpu_handles_set = true;
    }

    // Create render texture
    if (!yetty_wgpu_create_render_texture(width, height)) {
        spdlog::error("PythonLayer: Failed to create render texture");
        return false;
    }
    _texture_width = width;
    _texture_height = height;

    // Import and initialize yetty_pygfx module
    // First, add the build/python directory to sys.path
    auto result = _plugin->execute(
        "import sys\n"
        "sys.path.insert(0, '" + std::string(CMAKE_BINARY_DIR) + "/python')\n"
    );
    if (!result) {
        spdlog::error("PythonLayer: Failed to set Python path");
        return false;
    }

    // Import yetty_pygfx
    result = _plugin->execute(
        "import yetty_pygfx\n"
        "yetty_pygfx.init_pygfx()\n"
    );
    if (!result) {
        spdlog::error("PythonLayer: Failed to import yetty_pygfx: {}", result.error().message());
        return false;
    }

    // Create the figure
    std::string create_fig_code =
        "fig = yetty_pygfx.create_figure(" + std::to_string(width) + ", " + std::to_string(height) + ")\n";
    result = _plugin->execute(create_fig_code);
    if (!result) {
        spdlog::error("PythonLayer: Failed to create figure: {}", result.error().message());
        return false;
    }

    // Get the render_frame function for later use
    _pygfx_module = PyImport_ImportModule("yetty_pygfx");
    if (_pygfx_module) {
        _render_frame_func = PyObject_GetAttrString(_pygfx_module, "render_frame");
    }

    _pygfx_initialized = true;
    spdlog::info("PythonLayer: pygfx initialized with {}x{} render target", width, height);

    return true;
}

bool PythonLayer::renderPygfx() {
    if (!_pygfx_initialized || !_render_frame_func) {
        return false;
    }

    // Call render_frame()
    PyObject* result = PyObject_CallObject(_render_frame_func, nullptr);
    if (!result) {
        PyErr_Print();
        PyErr_Clear();
        return false;
    }

    bool success = PyObject_IsTrue(result);
    Py_DECREF(result);

    return success;
}

bool PythonLayer::createBlitPipeline(WebGPUContext& ctx) {
    if (_blit_initialized) return true;

    WGPUDevice device = ctx.getDevice();

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.maxAnisotropy = 1;

    _blit_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!_blit_sampler) {
        spdlog::error("PythonLayer: Failed to create blit sampler");
        return false;
    }

    // Simple fullscreen blit shader
    const char* shaderCode = R"(
        @group(0) @binding(0) var tex: texture_2d<f32>;
        @group(0) @binding(1) var samp: sampler;

        struct VertexOutput {
            @builtin(position) position: vec4f,
            @location(0) uv: vec2f,
        };

        @vertex
        fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
            var positions = array<vec2f, 6>(
                vec2f(-1.0, -1.0),
                vec2f( 1.0, -1.0),
                vec2f(-1.0,  1.0),
                vec2f(-1.0,  1.0),
                vec2f( 1.0, -1.0),
                vec2f( 1.0,  1.0)
            );
            var uvs = array<vec2f, 6>(
                vec2f(0.0, 1.0),
                vec2f(1.0, 1.0),
                vec2f(0.0, 0.0),
                vec2f(0.0, 0.0),
                vec2f(1.0, 1.0),
                vec2f(1.0, 0.0)
            );
            var out: VertexOutput;
            out.position = vec4f(positions[idx], 0.0, 1.0);
            out.uv = uvs[idx];
            return out;
        }

        @fragment
        fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
            return textureSample(tex, samp, uv);
        }
    )";

    WGPUShaderSourceWGSL wgslSource = {};
    wgslSource.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslSource.code = {.data = shaderCode, .length = strlen(shaderCode)};

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslSource.chain;

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shader) {
        spdlog::error("PythonLayer: Failed to create blit shader");
        return false;
    }

    // Bind group layout
    WGPUBindGroupLayoutEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 2;
    bglDesc.entries = entries;

    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = layout;

    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = {.data = "vs_main", .length = WGPU_STRLEN};

    WGPUFragmentState fragState = {};
    fragState.module = shader;
    fragState.entryPoint = {.data = "fs_main", .length = WGPU_STRLEN};

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = ctx.getSurfaceFormat();
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blend;

    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _blit_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(layout);
    wgpuBindGroupLayoutRelease(bgl);

    if (!_blit_pipeline) {
        spdlog::error("PythonLayer: Failed to create blit pipeline");
        return false;
    }

    _blit_initialized = true;
    spdlog::info("PythonLayer: Blit pipeline created");
    return true;
}

bool PythonLayer::blitRenderTexture(WebGPUContext& ctx) {
    // Get the render texture view
    WGPUTextureView texView = yetty_wgpu_get_render_texture_view();
    if (!texView) {
        return false;
    }

    // Create blit pipeline if needed
    if (!_blit_initialized) {
        if (!createBlitPipeline(ctx)) {
            return false;
        }
    }

    // Create bind group (recreate each frame in case texture changed)
    if (_blit_bind_group) {
        wgpuBindGroupRelease(_blit_bind_group);
        _blit_bind_group = nullptr;
    }

    // Get bind group layout from pipeline
    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(_blit_pipeline, 0);

    WGPUBindGroupEntry bgEntries[2] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = texView;

    bgEntries[1].binding = 1;
    bgEntries[1].sampler = _blit_sampler;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgEntries;

    _blit_bind_group = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    if (!_blit_bind_group) {
        spdlog::error("PythonLayer: Failed to create blit bind group");
        return false;
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encDesc);
    if (!encoder) return false;

    // Get current surface texture view
    auto surfaceViewResult = ctx.getCurrentTextureView();
    if (!surfaceViewResult) {
        wgpuCommandEncoderRelease(encoder);
        return false;
    }
    WGPUTextureView surfaceView = *surfaceViewResult;

    // Render pass
    WGPURenderPassColorAttachment colorAttach = {};
    colorAttach.view = surfaceView;
    colorAttach.loadOp = WGPULoadOp_Clear;
    colorAttach.storeOp = WGPUStoreOp_Store;
    colorAttach.clearValue = {0.0, 0.0, 0.0, 1.0};
    colorAttach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttach;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return false;
    }

    wgpuRenderPassEncoderSetPipeline(pass, _blit_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _blit_bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (cmd) {
        wgpuQueueSubmit(ctx.getQueue(), 1, &cmd);
        wgpuCommandBufferRelease(cmd);
    }
    wgpuCommandEncoderRelease(encoder);

    return true;
}

bool PythonLayer::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;

    if (action != 1) return false; // GLFW_PRESS only

    // Enter key - execute input buffer
    if (key == 257) { // GLFW_KEY_ENTER
        if (!_input_buffer.empty()) {
            auto result = _plugin->execute(_input_buffer);
            if (result) {
                _output += ">>> " + _input_buffer + "\n" + *result;
            } else {
                _output += ">>> " + _input_buffer + "\nError: " + result.error().message() + "\n";
            }
            _input_buffer.clear();
            return true;
        }
    }

    // Backspace - remove last character
    if (key == 259) { // GLFW_KEY_BACKSPACE
        if (!_input_buffer.empty()) {
            _input_buffer.pop_back();
            return true;
        }
    }

    return false;
}

bool PythonLayer::onChar(unsigned int codepoint) {
    if (codepoint < 128) {
        _input_buffer += static_cast<char>(codepoint);
        return true;
    }
    return false;
}

} // namespace yetty

extern "C" {
    const char* name() { return "python"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
        return yetty::PythonPlugin::create(std::move(engine));
    }
}
