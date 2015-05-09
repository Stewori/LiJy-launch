/*
 * jython.h
 *
 *  Created on: 26.04.2015
 *      Author: Stefan Richthofer
 */

#ifndef JYTHON_H_
#define JYTHON_H_

#include "java.h"

#define cstr_from_jstring(cstrName, jstr) \
	char* utf_string = (*env)->GetStringUTFChars(env, jstr, NULL); \
	char cstrName[strlen(utf_string)+1]; \
	strcpy(cstrName, utf_string); \
	(*env)->ReleaseStringUTFChars(env, jstr, utf_string)

#define setString(dest, name, value) \
	if (dest->name) { \
		free(dest->name); \
	} \
	dest->name = malloc(strlen(value)*sizeof(char)); \
	strcpy(dest->name, value)

#define setString0(dest, name, value) \
	dest->name = malloc(strlen(value)*sizeof(char)); \
	strcpy(dest->name, value)

#define executableOpt "-Dpython.executable="
#define unameOpt "-Dpython.launcher.uname="
#define homeOpt "-Dpython.home="
#define ttyOpt "-Dpython.launcher.tty="
#define consoleOpt "-Dpython.console="
#define consoleOptVal "-Dpython.console=org.python.core.PlainConsole"
#define defaultMem "-Xmx512m"
#define defaultStack "-Xss1024k"

#define checkProperty(checkSource, propertyDef, checkDest) \
	if (strncmp(checkSource+2, propertyDef+2, sizeof(propertyDef)-3) == 0) { \
		checkDest = JNI_TRUE; \
	}

//Note that propertyDef must be a string-constant such that sizeof works.
#define declareJavaOption(propertyDef, value, condition) \
	char _ ## propertyDef[condition ? 0 : sizeof(propertyDef)+strlen(value)]

//Note that propertyDef must be a string-constant such that sizeof works.
#define setJavaOption(propertyDef, value) \
	strcpy(_ ## propertyDef, propertyDef); \
	strcpy(_ ## propertyDef+sizeof(propertyDef)-1, value); \
	AddOption(_ ## propertyDef, NULL)

#define setJavaOptionArg(property, value) \
	char _ ## propertyDef[sizeof(propertyDef)+strlen(value)]; \
	strcpy(_ ## propertyDef, propertyDef); \
	strcpy(_ ## propertyDef+sizeof(propertyDef)-1, value); \
	AddOption(_ ## propertyDef, NULL)

int Jython_Main(int argc, char ** argv,         /* main argc, argc */
        int jargc, const char** jargv,          /* java args */
        int appclassc, const char** appclassv,  /* app classpath */
        const char* pname,                      /* program name */
        const char* lname,                      /* launcher name */
        jboolean javaargs,                      /* JAVA_ARGS */
        jboolean cpwildcard,                    /* classpath wildcard */
        jboolean javaw,                         /* windows-only javaw */
        jint     ergo_class                     /* ergnomics policy */
);

typedef struct {
	jboolean boot;
	jboolean jdb;
	jboolean help;
	jboolean print_requested;
	jboolean profile;
	jboolean tty;
//Determine whether defaults for some certain
//propertys should be set-up:
	jboolean pythonHomeInArgs;
	jboolean unameInArgs;
	jboolean executableInArgs;
	jboolean ttyInArgs;
	jboolean consoleInArgs;
	int propCount;
	//char** propValues;
	//char** propKeys;
	char** properties;
	int javaCount;
	char** java;
	int jythonCount;
	char** jython;
	char* cp;
	char* mem;
	char* stack;
	char* progName;
	char* uname;
} JySetup;

JySetup* parse_launcher_args(int argc, char** args);
void freeSetup(JySetup* setup);
void printSetup(JySetup* js);
void print_help();
void bad_option(char* msg);

int cygpathCall(char* path, char* dest);
void prepareClasspath(JySetup* setup, char* jythonHome,
		char* jythonJar, jboolean boot, jboolean freeOld);
void prepareJdbClasspath(char* jrePath, char* dest);
int prepareJdbClasspathLen(char* jrePath);

int
JLI_Launch(int argc, char ** argv,              /* main argc, argc */
        int jargc, const char** jargv,          /* java args */
        int appclassc, const char** appclassv,  /* app classpath */
        const char* pname,                      /* program name */
        const char* lname,                      /* launcher name */
        jboolean javaargs,                      /* JAVA_ARGS */
        jboolean cpwildcard,                    /* classpath wildcard */
        jboolean javaw,                         /* windows-only javaw */
        jint     ergo_class,                    /* ergnomics policy */
        JySetup* jysetup
);

/*
 * Different platforms will implement this, here
 * pargc is a pointer to the original argc,
 * pargv is a pointer to the original argv,
 * jrepath is an accessible path to the jre as determined by the call
 * so_jrepath is the length of the buffer jrepath
 * jvmpath is an accessible path to the jvm as determined by the call
 * so_jvmpath is the length of the buffer jvmpath
 */
void CreateExecutionEnvironment(int *argc, char ***argv,
                                char *jrepath, jint so_jrepath,
                                char jypath[], jint so_jypath,
                                char *jvmpath, jint so_jvmpath,
                                char *jvmcfg,  jint so_jvmcfg,
                                JySetup* jysetup);

#endif /* JYTHON_H_ */
