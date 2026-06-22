#pragma once

#ifndef EASYTOOLS_CORE_UTILS_EXPORT_H
#define EASYTOOLS_CORE_UTILS_EXPORT_H

#if defined(_WIN32)
    #if defined(EASYCORE_EXPORTS)
        #define EASYCORE_API __declspec(dllexport)
    #else
        #define EASYCORE_API __declspec(dllimport)
    #endif

    #if defined(PLUGIN_EXPORTS)
        #define PLUGIN_API extern "C" __declspec(dllexport)
    #else
        #define PLUGIN_API extern "C" __declspec(dllimport)
    #endif
#else
    #define EASYCORE_API
    #define PLUGIN_API
#endif

#endif // EASYTOOLS_CORE_UTILS_EXPORT_H
