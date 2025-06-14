///|/ Copyright (c) Prusa Research 2022 Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "STEP.hpp"
#include "occt_wrapper/OCCTWrapper.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/log/trivial.hpp>

#include <string>
#include <functional>

#ifdef _WIN32
    #include<windows.h>
#else
    #include<occt_wrapper/OCCTWrapper.hpp>
    #include <dlfcn.h>
#endif


namespace Slic3r {

#if __APPLE__
extern "C" bool load_step_internal(const char *path, OCCTResult* res);
#endif

LoadStepFn get_load_step_fn()
{
    static LoadStepFn load_step_fn = nullptr;

#ifndef __APPLE__
    constexpr const char* fn_name = "load_step_internal";
#endif

    if (!load_step_fn) {
        boost::filesystem::path libpath = boost::dll::program_location().parent_path();
#ifdef _WIN32
        libpath /= "OCCTWrapper.dll";
        HMODULE module = LoadLibraryW(libpath.wstring().c_str());
        if (module == NULL)
            throw Slic3r::RuntimeError("Cannot load OCCTWrapper.dll");

        try {
            FARPROC farproc = GetProcAddress(module, fn_name);
            if (! farproc) {
                DWORD ec = GetLastError();
                throw Slic3r::RuntimeError(std::string("Cannot load function from OCCTWrapper.dll: ") + fn_name
                                           + "\n\nError code: " + std::to_string(ec));
            }
            load_step_fn = reinterpret_cast<LoadStepFn>(farproc);
        } catch (const Slic3r::RuntimeError&) {
            FreeLibrary(module);
            throw;
        }
#elif __APPLE__
        load_step_fn = &load_step_internal;
#else
        // first try with system install directory
        if (libpath.filename() == "bin") {
            libpath = libpath.parent_path();
            libpath /= "lib";
        }
        libpath /= "superslicer";
        libpath /= "OCCTWrapper.so";
        BOOST_LOG_TRIVIAL(debug) << "Exsists"<<libpath.string()<< " ? "<<boost::filesystem::exists(libpath);
        if (!boost::filesystem::exists(libpath)) {
            boost::filesystem::path libpath_old = libpath;
            // retry with appimage directory
            libpath = boost::dll::program_location().parent_path();
            libpath /= "OCCTWrapper.so";
            BOOST_LOG_TRIVIAL(debug) << "Exsists"<<libpath.string()<< " ? "<<boost::filesystem::exists(libpath);
            if (!boost::filesystem::exists(libpath)) {
                BOOST_LOG_TRIVIAL(error) << "Error: can't find '"<<libpath_old.string()<< "' nor '"<<libpath.string()<<"' to load.";
                libpath = "OCCTWrapper.so";
            }
        }

        BOOST_LOG_TRIVIAL(debug) << "try to open dll "<<libpath.c_str()<< " ? "<<boost::filesystem::exists(libpath);
        void *plugin_ptr = dlopen(libpath.c_str(), RTLD_NOW | RTLD_GLOBAL);

        if (plugin_ptr) {
            load_step_fn = reinterpret_cast<LoadStepFn>(dlsym(plugin_ptr, fn_name));
            if (!load_step_fn) {
                dlclose(plugin_ptr);
                throw Slic3r::RuntimeError(std::string("Cannot load function from OCCTWrapper.so: ") + fn_name
                                           + "\n\n" + dlerror());
            }
        } else {
            throw Slic3r::RuntimeError(std::string("Cannot load OCCTWrapper.so:\n\n") + dlerror());
        }
#endif
    }

    return load_step_fn;
}

bool load_step(const char *path, Model *model /*BBS:, ImportStepProgressFn proFn*/)
{
    OCCTResult occt_object;

    LoadStepFn load_step_fn = get_load_step_fn();

    if (!load_step_fn)
        return false;

    load_step_fn(path, &occt_object);

    assert(! occt_object.volumes.empty());
    
    assert(boost::algorithm::iends_with(occt_object.object_name, ".stp")
        || boost::algorithm::iends_with(occt_object.object_name, ".step"));
    occt_object.object_name.erase(occt_object.object_name.find("."));
    assert(! occt_object.object_name.empty());


    ModelObject* new_object = model->add_object();
    new_object->input_file = path;
    if (new_object->volumes.size() == 1 && ! occt_object.volumes.front().volume_name.empty())
        new_object->name = new_object->volumes.front()->name;
    else
        new_object->name = occt_object.object_name;


    for (size_t i=0; i<occt_object.volumes.size(); ++i) {
        indexed_triangle_set its;
        for (size_t j=0; j<occt_object.volumes[i].vertices.size(); ++j)
            its.vertices.emplace_back(Vec3f(occt_object.volumes[i].vertices[j][0],
                                            occt_object.volumes[i].vertices[j][1],
                                            occt_object.volumes[i].vertices[j][2]));
        for (size_t j=0; j<occt_object.volumes[i].indices.size(); ++j)
            its.indices.emplace_back(Vec3i32(occt_object.volumes[i].indices[j][0],
                                           occt_object.volumes[i].indices[j][1],
                                           occt_object.volumes[i].indices[j][2]));
        its_merge_vertices(its, true);
        TriangleMesh triangle_mesh(std::move(its));
        ModelVolume* new_volume = new_object->add_volume(std::move(triangle_mesh));

        new_volume->name = occt_object.volumes[i].volume_name.empty()
                       ? std::string("Part") + std::to_string(i+1)
                       : occt_object.volumes[i].volume_name;
        new_volume->source.input_file = path;
        new_volume->source.object_idx = (int)model->objects.size() - 1;
        new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
    }

    return true;
}

}; // namespace Slic3r
