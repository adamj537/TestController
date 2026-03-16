/**
 * @file unity_config.h
 * @brief Unity test framework configuration for native (CI/CD) builds.
 *
 * Required by the throwtheswitch/Unity library when UNITY_INCLUDE_CONFIG_H
 * is defined. PlatformIO's Unity build script (platformio-build.py) searches
 * CPPPATH for this file; placing it in include/ ensures it is found before the
 * Unity library compiles — avoiding the build-ordering issue that arises when
 * lib_dir = . is set at the platformio level.
 *
 * For native builds, Unity outputs to stdout via stdio.
 */
#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#ifndef NULL
#ifndef __cplusplus
#define NULL (void*)0
#else
#define NULL 0
#endif
#endif

#ifdef __cplusplus
extern "C"
{
#endif

void unityOutputStart(unsigned long baudrate);
void unityOutputChar(unsigned int c);
void unityOutputFlush(void);
void unityOutputComplete(void);

#define UNITY_OUTPUT_START()    unityOutputStart(115200UL)
#define UNITY_OUTPUT_CHAR(c)    unityOutputChar(c)
#define UNITY_OUTPUT_FLUSH()    unityOutputFlush()
#define UNITY_OUTPUT_COMPLETE() unityOutputComplete()

#ifdef __cplusplus
}
#endif /* extern "C" */

#endif /* UNITY_CONFIG_H */
