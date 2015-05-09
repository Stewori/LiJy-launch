/*
 * Copyright (c) 1995, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/*
 * Shared source for 'java' command line tool.
 *
 * If JAVA_ARGS is defined, then acts as a launcher for applications. For
 * instance, the JDK command line tools such as javac and javadoc (see
 * makefiles for more details) are built with this program.  Any arguments
 * prefixed with '-J' will be passed directly to the 'java' command.
 */

/*
 * One job of the launcher is to remove command line options which the
 * vm does not understand and will not process.  These options include
 * options which select which style of vm is run (e.g. -client and
 * -server) as well as options which select the data model to use.
 * Additionally, for tools which invoke an underlying vm "-J-foo"
 * options are turned into "-foo" options to the vm.  This option
 * filtering is handled in a number of places in the launcher, some of
 * it in machine-dependent code.  In this file, the function
 * CheckJvmType removes vm style options and TranslateApplicationArgs
 * removes "-J" prefixes.  The CreateExecutionEnvironment function processes
 * and removes -d<n> options. On unix, there is a possibility that the running
 * data model may not match to the desired data model, in this case an exec is
 * required to start the desired model. If the data models match, then
 * ParseArguments will remove the -d<n> flags. If the data models do not match
 * the CreateExecutionEnviroment will remove the -d<n> flags.
 */


#include "jython.h"
//#include "glob.h"

/*
 * A NOTE TO DEVELOPERS: For performance reasons it is important that
 * the program image remain relatively small until after SelectVersion
 * CreateExecutionEnvironment have finished their possibly recursive
 * processing. Watch everything, but resist all temptations to use Java
 * interfaces.
 */

/* we always print to stderr */
#define USE_STDERR JNI_TRUE

static jboolean printVersion = JNI_FALSE; /* print and exit */
static jboolean showVersion = JNI_FALSE;  /* print but continue */
static jboolean printUsage = JNI_FALSE;   /* print and exit*/
static jboolean printXUsage = JNI_FALSE;  /* print and exit*/
static char	 *showSettings = NULL;        /* print but continue */

static const char *_program_name;
static const char *_launcher_name;
static jboolean _is_java_args = JNI_FALSE;
static jboolean _wc_enabled = JNI_FALSE;
static jint _ergo_policy = DEFAULT_POLICY;

/*
 * List of VM options to be specified when the VM is created.
 */
static JavaVMOption *options;
static int numOptions, maxOptions;

/*
 * Prototypes for functions internal to launcher.
 */
static void SetClassPath(const char *s, jboolean expand, jboolean boot);
static jboolean ParseArguments(int *pargc, char ***pargv,
							   int *pmode, char **pwhat,
							   int *pret, const char *jrepath);
static jboolean InitializeJVM(JavaVM **pvm, JNIEnv **penv,
							  InvocationFunctions *ifn);
static jstring NewPlatformString(JNIEnv *env, char *s);
static jclass LoadMainClass(JNIEnv *env, int mode, char *name);
//static jclass GetApplicationClass(JNIEnv *env);

//static void TranslateApplicationArgs(int jargc, const char **jargv, int *pargc, char ***pargv);
static jboolean AddApplicationOptions(int cpathc, const char **cpathv);

static void PrintJavaVersion(JNIEnv *env, jboolean extraLF);
static void PrintUsage(JNIEnv* env, jboolean doXUsage);
static void ShowSettings(JNIEnv* env, char *optString);

static void DumpState();
static jboolean RemovableOption(char *option);

/* Maximum supported entries from jvm.cfg. */
#define INIT_MAX_KNOWN_VMS	  10

/* Values for vmdesc.flag */
enum vmdesc_flag {
	VM_UNKNOWN = -1,
	VM_KNOWN,
	VM_ALIASED_TO,
	VM_WARN,
	VM_ERROR,
	VM_IF_SERVER_CLASS,
	VM_IGNORE
};

struct vmdesc {
	char *name;
	int flag;
	char *alias;
	char *server_class;
};
static struct vmdesc *knownVMs = NULL;
static int knownVMsCount = 0;
static int knownVMsLimit = 0;

static void GrowKnownVMs();
static int  KnownVMIndex(const char* name);
static void FreeKnownVMs();
static jboolean IsWildCardEnabled();

#define ARG_CHECK(AC_arg_count, AC_failure_message, AC_questionable_arg) \
	do { \
		if (AC_arg_count < 1) { \
			JLI_ReportErrorMessage(AC_failure_message, AC_questionable_arg); \
			printUsage = JNI_TRUE; \
			*pret = 1; \
			return JNI_TRUE; \
		} \
	} while (JNI_FALSE)

/*
 * Running Java code in primordial thread caused many problems. We will
 * create a new thread to invoke JVM. See 6316197 for more information.
 */
static jlong threadStackSize	= 0;  /* stack size of the new thread */
static jlong maxHeapSize		= 0;  /* max heap size */
static jlong initialHeapSize	= 0;  /* inital heap size */

#define cpOption0 "-Djava.class.path="
#define jythonClass "org.python.util.jython"
#define jythonClassP "org/python/util/jython"
#define profileOptPre "-javaagent:"
#define profileOptPost "/javalib/profile.jar"

/*
 * Entry point.
 */
int
JLI_Launch(int argc, char ** argv,             /* main argc, argc */
		int jargc, const char** jargv,         /* java args */
		int appclassc, const char** appclassv, /* app classpath */
		const char* pname,                     /* program name */
		const char* lname,                     /* launcher name */
		jboolean javaargs,                     /* JAVA_ARGS */
		jboolean cpwildcard,                   /* classpath wildcard*/
		jboolean javaw,                        /* windows-only javaw */
		jint ergo,                             /* ergonomics class policy */
		JySetup* jysetup
)
{
//	puts("JLI_launch");
//	puts(pname);
//	puts(lname);
	int mode = LM_CLASS;//LM_UNKNOWN;
	char *what = jythonClassP;
	//char *cpath = 0;
	//char *main_class = NULL;
	int ret;
	InvocationFunctions ifn;
	jlong start, end;
	char jvmpath[MAXPATHLEN];
	char jrepath[MAXPATHLEN];
	char jypath[MAXPATHLEN];
	char jvmcfg[MAXPATHLEN];

	_launcher_name = lname;
	_program_name = pname;
	_is_java_args = javaargs;
	_wc_enabled = cpwildcard;
	_ergo_policy = ergo;

	InitLauncher(javaw);
	DumpState();
	int i;
	if (JLI_IsTraceLauncher()) {
//		int i;
//		printf("Command line args:\n");
//		for (i = 0; i < argc ; i++) {
//			printf("argv[%d] = %s\n", i, argv[i]);
//		}
		AddOption("-Dsun.java.launcher.diag=true", NULL);
	}

	AddOption(jysetup->mem, NULL);
	AddOption(jysetup->stack, NULL);
	/*
	 * Make sure the specified version of the JRE is running.
	 *
	 * There are three things to note about the SelectVersion() routine:
	 *  1) If the version running isn't correct, this routine doesn't
	 *	 return (either the correct version has been exec'd or an error
	 *	 was issued).
	 *  2) Argc and Argv in this scope are *not* altered by this routine.
	 *	 It is the responsibility of subsequent code to ignore the
	 *	 arguments handled by this routine.
	 *  3) As a side-effect, the variable "main_class" is guaranteed to
	 *	 be set (if it should ever be set).  This isn't exactly the
	 *	 poster child for structured programming, but it is a small
	 *	 price to pay for not processing a jar file operand twice.
	 *	 (Note: This side effect has been disabled.  See comment on
	 *	 bugid 5030265 below.)
	 */
	//SelectVersion(argc, argv, &main_class);
//	puts(getenv("JAVA_HOME"));
//	puts("architecture:");
//	puts(LIBARCHNAME);
//	printf("Command line args0:\n");
//	int i;
//	for (i = 0; i < argc ; i++) {
//		printf("argv[%d] = %s\n", i, argv[i]);
//	}
	CreateExecutionEnvironment(&argc, &argv,
							   jrepath, sizeof(jrepath),
							   jypath, sizeof(jypath),
							   jvmpath, sizeof(jvmpath),
							   jvmcfg,  sizeof(jvmcfg),
							   jysetup);
//	Evironment info:
//	/home/stefan/eclipseWorkspace/LiJy-launch/jre
//	/home/stefan/eclipseWorkspace/LiJy-launch/jre/lib/amd64/server/libjvm.so
//	/home/stefan/eclipseWorkspace/LiJy-launch/jre/lib/amd64/jvm.cfg
//	puts("Evironment info:");
//	puts(jrepath);
//	puts(jypath);
//	puts(jvmpath);
//	puts(jvmcfg);
	char* _jydir = strrchr(jypath, '/');
	char jydir[_jydir-jypath];
	strncpy(jydir, jypath, _jydir-jypath);
	jydir[_jydir-jypath] = 0;
	int cygpathlen = jysetup->uname && strcmp(jysetup->uname, "cygwin") == 0 ?
			PATH_MAX : 0;
	char cygpath[cygpathlen];
	if (cygpathlen) {
		cygpathCall(jydir, cygpath);
		//todo: Handle result of cygpathCall,
		//i.e. fp == NULL or result of pclose == -1
	}
//	puts(jydir);
//	puts("\nClasspath-info:");
//	puts(jysetup->cp);
//	prepareClasspath(jysetup, jydir, jypath, JNI_FALSE, JNI_TRUE);
//	puts(jysetup->cp);
//	SetClassPath(jysetup->cp, JNI_FALSE);
	if (jysetup->boot) {
		//This might look a bit cumbersome. Creating the boot-classpath
		//by using the first boolean JNI_TRUE means that we will need the
		//original value of jysetup->cp. So we must tell with the second
		//boolean JNI_FALSE that prepareClasspath won't free it up before
		//we're done with it. So now freeing it up is our job and we do
		//it below.
		char* ocp = jysetup->cp;
		prepareClasspath(jysetup, jydir, jypath, JNI_TRUE, JNI_FALSE);
		SetClassPath(ocp, !jysetup->print_requested, JNI_FALSE);
		free(ocp);
		SetClassPath(jysetup->cp, JNI_TRUE, JNI_TRUE);
	} else {
		prepareClasspath(jysetup, jydir, jypath, JNI_FALSE, JNI_TRUE);
		SetClassPath(jysetup->cp, !jysetup->print_requested, JNI_FALSE);
	}

	ifn.CreateJavaVM = 0;
	ifn.GetDefaultJavaVMInitArgs = 0;

	if (JLI_IsTraceLauncher()) {
		start = CounterGet();
	}

	if (!LoadJavaVM(jvmpath, &ifn)) {
		return(6);
	}
	if (JLI_IsTraceLauncher()) {
		end = CounterGet();
	}

	JLI_TraceLauncher("%ld micro seconds to LoadJavaVM\n",
			 (long)(jint)Counter2Micros(end-start));
	//++argv;
	//--argc;

	if (!AddApplicationOptions(appclassc, appclassv)) {
		return(1);
	}
//	if (IsJavaArgs()) {
//		/* Preprocess wrapper arguments */
//		TranslateApplicationArgs(jargc, jargv, &argc, &argv);
//		if (!AddApplicationOptions(appclassc, appclassv)) {
//			return(1);
//		}
//	} else {
//		/* Set default CLASSPATH */
//		cpath = getenv("CLASSPATH");
//		if (cpath == NULL) {
//			cpath = ".";
//		}
//		SetClassPath(cpath, JNI_FALSE);
//	}

	/* Parse command line options; if the return value of
	 * ParseArguments is false, the program should exit.
	 */
	//todo: Here jysetup->java can/should be consumed
//	if (!ParseArguments(&argc, &argv, &mode, &what, &ret, jrepath))
//	{
//		return(ret);
//	}
//	puts("what:");
//	puts(what);
//	/* Override class path if -jar flag was specified */
////	if (mode == LM_JAR) {
////		SetClassPath(what);	 /* Override class path */
////	}
//	//SetClassPath(jypath, JNI_FALSE);
//	/* set the -Dsun.java.command pseudo property */
//	printf("Command line args2:\n");
//	for (i = 0; i < argc ; i++) {
//		printf("argv[%d] = %s\n", i, argv[i]);
//	}
//	puts("jars for CLASSPATH:");
//	glob_t globbuf;
//	glob("/home/stefan/eclipseWorkspace/Jython/jython/dist/javalib/*.jar", 0, NULL, &globbuf);
//	//'/home/stefan/eclipseWorkspace/Jython/jython/dist/javalib'
//	for (i = 0; i < globbuf.gl_pathc; ++i) {
//		puts(globbuf.gl_pathv[i]);
//	}
//	globfree(&globbuf);

	//todo: Fix this for jdb-case!
	//SetJavaCommandLineProp(what, argc, argv);
	SetJavaCommandLineProp(jythonClass, jysetup->jythonCount, jysetup->jython);
	/* Set the -Dsun.java.launcher pseudo property */
	SetJavaLauncherProp();
	const char *execname = GetExecName();
	declareJavaOption(executableOpt, execname, jysetup->executableInArgs);
	if (!jysetup->executableInArgs) {
		setJavaOption(executableOpt, execname);
	}
	declareJavaOption(unameOpt, jysetup->uname, jysetup->unameInArgs);
	if (!jysetup->unameInArgs) {
		setJavaOption(unameOpt, jysetup->uname);
	}
	const char *tty = jysetup->tty ? "true" : "false";
	declareJavaOption(ttyOpt, tty, jysetup->ttyInArgs);
	if (!jysetup->ttyInArgs) {
		setJavaOption(ttyOpt, tty);
	}
	declareJavaOption(homeOpt, cygpathlen == 0 ? jydir : cygpath, jysetup->pythonHomeInArgs);
	if (!jysetup->pythonHomeInArgs) {
		setJavaOption(homeOpt, cygpathlen == 0 ? jydir : cygpath);
	}
	if (jysetup->uname && !jysetup->consoleInArgs && strcmp(jysetup->uname, "cygwin") == 0) {
		AddOption(consoleOptVal, NULL);
	}
	if (jysetup->properties) {
		for (i = 0; i < jysetup->propCount; ++i) {
			AddOption(jysetup->properties[i], NULL);
		}
	}
	/* set the -Dsun.java.launcher.* platform properties */
	SetJavaLauncherPlatformProps();

	char* profileOpt = "-XX:-UseSplitVerifier";
	char* profileOpt2 = NULL;
	if (jysetup->profile) {
		profileOpt2 = malloc(sizeof(profileOptPre)
				+sizeof(profileOptPost)+(_jydir-jypath)-1);
		strcpy(profileOpt2, profileOptPre);
		strcpy(profileOpt2+sizeof(profileOptPre)-1, jydir);
		strcpy(profileOpt2+sizeof(profileOptPre)+(_jydir-jypath)-1,
				profileOptPost);
//		puts("profileOpt:");
//		puts(profileOpt2);
//		printf("%i %i\n", sizeof(profileOptPre)
//				+sizeof(profileOptPost)+(_jydir-jypath)-1, strlen(profileOpt2));
		AddOption(profileOpt, NULL);
		AddOption(profileOpt2, NULL);
	}
//	puts("\nOptions:");
////	static JavaVMOption *options;
////	static int numOptions
//	for (i = 0; i < numOptions; ++i)
//	{
//		puts(options[i].optionString);
//	}
//	puts("\n");
//
//	puts("argv/argc before JVMInit:");
//	printf("App's argc is %d\n", argc);
//	for (i=0; i < argc; i++)
//	{
//		printf("	argv[%2d] = '%s'\n", i, argv[i]);
//	}
	if (jysetup->print_requested)
	{
		//yes, we destroy jrepath here, but it won't be needed again
		jrepath[strrchr(jrepath, FILE_SEPARATOR)-jrepath] = NULL;
		printf(jrepath);
		printf(FILESEP);
		printf("bin");
		printf(FILESEP);
		if (jysetup->jdb)
			printf("jdb ");
		else
			printf("java ");
		for (i = 0; i < numOptions; ++i) {
			if (strncmp(options[i].optionString, cpOption0, sizeof(cpOption0)-1) == 0) {
				printf("-classpath ");
				printf("%s ", options[i].optionString+sizeof(cpOption0)-1);
			} else {
				//for ordinary options (other than classpath,
				//whitespaces can be cured by quotation marks.
				if (strchr(options[i].optionString, ' ')) {
					int len = strlen(options[i].optionString);
					char* delim = strchr(options[i].optionString, '=');
					delim[0] = NULL;
					printf("%s=\"%s\" ", options[i].optionString, delim+1);
				} else {
					printf("%s ", options[i].optionString);
				}
				//newArgv[pos++] = options[i].optionString;
			}
		}
		printf("%s ", jythonClass);
		for (i = 0; i < jysetup->jythonCount; ++i) {
			printf("%s ", jysetup->jython[i]);
		}
		printf("\n");
		return 0;
	}
	if (jysetup->jdb)
	{
		/*
		 * What we have to do here is to take all original options
		 * and prepend them to ordinary java-args, such that new
		 * java-args consist of the following:
		 * - options
		 * - original what (i.e. org/python/util/jython)
		 * - original java-args
		 *
		 * Note that the classpath-option must be converted from
		 * -Djava.class.path=...-format to
		 * -classpath ...
		 * in order to be recognized by jdb (not even -cp ... works).
		 *
		 * Then we'll have to create new options such that classpath is
		 * /usr/local/java/jdk1.8.0_25/lib/tools.jar:
		 * /usr/local/java/jdk1.8.0_25/lib/sa-jdi.jar:
		 * /usr/local/java/jdk1.8.0_25/jre/classes
		 *
		 * Options are
		 *  -ms8m
		 *
		 * And new what is
		 *  com/sun/tools/example/debug/tty/TTY
		 */
		//int newArgc = argc+numOptions+2;
		int newArgc = jysetup->jythonCount+numOptions+2;
		char* newArgv[newArgc];
		int pos = 0;
		char* toFree[newArgc];
		for (i = 0; i < numOptions; ++i) {
			if (strncmp(options[i].optionString, cpOption0, sizeof(cpOption0)-1) == 0) {
				toFree[pos] = NULL;
				newArgv[pos++] = "-classpath";
				char* cp0 = options[i].optionString+sizeof(cpOption0)-1;
		//note that jdb does not support classpath with whitespaces
		//even not with quotation-marks!
//				if (strchr(cp0, ' ')) {
//					int len = strlen(cp0);
//					toFree[pos] = malloc(len+3);
//					toFree[pos][0] = '"';
//					strcpy(toFree[pos]+1, cp0);
//					toFree[pos][1+len] = '"';
//					toFree[pos][2+len] = 0;
//					newArgv[pos] = toFree[pos];
//					++pos;
//				} else {
				toFree[pos] = NULL;
				newArgv[pos++] = cp0;
//				}
			} else {
				//for ordinary options (other than classpath,
				//whitespaces can be cured by quotation marks.
				if (strchr(options[i].optionString, ' ')) {
					int len = strlen(options[i].optionString);
					char* delim = strchr(options[i].optionString, '=');
					int len1 = delim-options[i].optionString+1;
					toFree[pos] = malloc(len+3);
					strncpy(toFree[pos], options[i].optionString, len1);
					toFree[pos][len1] = '"';
					strcpy(toFree[pos]+len1+1, delim+1);
					toFree[pos][1+len] = '"';
					toFree[pos][2+len] = 0;
					newArgv[pos] = toFree[pos];
					++pos;
				} else {
					toFree[pos] = NULL;
					newArgv[pos++] = options[i].optionString;
				}
				//newArgv[pos++] = options[i].optionString;
			}
		}
		newArgv[pos++] = jythonClass;//what;
		for (i = 0; i < jysetup->jythonCount; ++i) {
			newArgv[pos++] = jysetup->jython[i];
		}
		//this code prepares the new classpath:
		numOptions = 2;
		AddOption("-Xms8m", NULL);
		int jdpCpLen = prepareJdbClasspathLen(jrepath);
		char jdpCp[jdpCpLen];
		prepareJdbClasspath(jrepath, &jdpCp);
		SetClassPath(jdpCp, JNI_TRUE, JNI_FALSE); //maybe better use boot here
		what = "com/sun/tools/example/debug/tty/TTY";
//		puts("Debug options:");
//		for (i = 0; i < numOptions; ++i) {
//			puts(options[i].optionString);
//		}
//		puts("\nwhat");
//		puts(what);
//		puts("argv/argc before JVMInit:");
//		printf("App's argc is %d\n", argc);
//		for (i=0; i < newArgc; i++) {
//			printf("	argv[%2d] = '%s'\n", i, newArgv[i]);
//		}
		int result = JVMInit(&ifn, threadStackSize, newArgc, newArgv,
					mode, what, ret, jysetup->help);
		for (i = 0; i < newArgc; ++i) {
			if (toFree[newArgc]) free(toFree[newArgc]);
		}
		if (profileOpt2) free(profileOpt2);
		return result;
	}

	int result = JVMInit(&ifn, threadStackSize,
			jysetup->jythonCount, jysetup->jython,// argc, argv,
			mode, what, ret, jysetup->help);
	if (profileOpt2) free(profileOpt2);
	return result;
}

/*
 * Always detach the main thread so that it appears to have ended when
 * the application's main method exits.  This will invoke the
 * uncaught exception handler machinery if main threw an
 * exception.  An uncaught exception handler cannot change the
 * launcher's return code except by calling System.exit.
 *
 * Wait for all non-daemon threads to end, then destroy the VM.
 * This will actually create a trivial new Java waiter thread
 * named "DestroyJavaVM", but this will be seen as a different
 * thread from the one that executed main, even though they are
 * the same C thread.  This allows mainThread.join() and
 * mainThread.isAlive() to work as expected.
 */
#define LEAVE() \
	do { \
		if ((*vm)->DetachCurrentThread(vm) != JNI_OK) { \
			JLI_ReportErrorMessage(JVM_ERROR2); \
			ret = 1; \
		} \
		if (JNI_TRUE) { \
			(*vm)->DestroyJavaVM(vm); \
			return ret; \
		} \
	} while (JNI_FALSE)

#define CHECK_EXCEPTION_NULL_LEAVE(CENL_exception) \
	do { \
		if ((*env)->ExceptionOccurred(env)) { \
			JLI_ReportExceptionDescription(env); \
			LEAVE(); \
		} \
		if ((CENL_exception) == NULL) { \
			JLI_ReportErrorMessage(JNI_ERROR); \
			LEAVE(); \
		} \
	} while (JNI_FALSE)

#define CHECK_EXCEPTION_LEAVE(CEL_return_value) \
	do { \
		if ((*env)->ExceptionOccurred(env)) { \
			JLI_ReportExceptionDescription(env); \
			ret = (CEL_return_value); \
			LEAVE(); \
		} \
	} while (JNI_FALSE)

int JNICALL
JavaMain(void * _args)
{
	//puts("JavaMain");
	JavaMainArgs *args = (JavaMainArgs *)_args;
	int argc = args->argc;
	char **argv = args->argv;
	int mode = args->mode;
	char *what = args->what;
	InvocationFunctions ifn = args->ifn;

//	puts("argv/argc at JavaMain:");
//	int i0;
//	printf("App's argc is %d\n", argc);
//	for (i0=0; i0 < argc; i0++) {
//		printf("	argv[%2d] = '%s'\n", i0, argv[i0]);
//	}

	JavaVM *vm = 0;
	JNIEnv *env = 0;
	jclass mainClass = NULL;
	jmethodID mainID;
	jobjectArray mainArgs;
	int ret = 0;
	jlong start, end;

	RegisterThread();

	/* Initialize the virtual machine */
	start = CounterGet();
	if (!InitializeJVM(&vm, &env, &ifn)) {
		JLI_ReportErrorMessage(JVM_ERROR1);
		exit(1);
	}
	if (showSettings != NULL) {
		ShowSettings(env, showSettings);
		CHECK_EXCEPTION_LEAVE(1);
	}
	if (printVersion || showVersion) {
		PrintJavaVersion(env, showVersion);
		CHECK_EXCEPTION_LEAVE(0);
		if (printVersion) {
			LEAVE();
		}
	}
	/* If the user specified neither a class name nor a JAR file */
	if (printXUsage || printUsage || what == 0 || mode == LM_UNKNOWN) {
		PrintUsage(env, printXUsage);
		CHECK_EXCEPTION_LEAVE(1);
		LEAVE();
	}
//	puts("printUsage done");
	FreeKnownVMs();  /* after last possible PrintUsage() */
//	puts("FreeKnownVMs done");
	if (JLI_IsTraceLauncher()) {
		end = CounterGet();
		JLI_TraceLauncher("%ld micro seconds to InitializeJVM\n",
			   (long)(jint)Counter2Micros(end-start));
	}

	/* At this stage, argc/argv have the application's arguments */
	if (JLI_IsTraceLauncher()){
//		int i;
//		//printf("%s is '%s'\n", launchModeNames[mode], what);
//		printf("App's argc is %d\n", argc);
//		for (i=0; i < argc; i++) {
//			printf("	argv[%2d] = '%s'\n", i, argv[i]);
//		}
	}
	ret = 1;

	/*
	 * Get the application's main class.
	 *
	 * See bugid 5030265.  The Main-Class name has already been parsed
	 * from the manifest, but not parsed properly for UTF-8 support.
	 * Hence the code here ignores the value previously extracted and
	 * uses the pre-existing code to reextract the value.  This is
	 * possibly an end of release cycle expedient.  However, it has
	 * also been discovered that passing some character sets through
	 * the environment has "strange" behavior on some variants of
	 * Windows.  Hence, maybe the manifest parsing code local to the
	 * launcher should never be enhanced.
	 *
	 * Hence, future work should either:
	 *	 1)   Correct the local parsing code and verify that the
	 *		  Main-Class attribute gets properly passed through
	 *		  all environments,
	 *	 2)   Remove the vestages of maintaining main_class through
	 *		  the environment (and remove these comments).
	 *
	 * This method also correctly handles launching existing JavaFX
	 * applications that may or may not have a Main-Class manifest entry.
	 */
	mainClass = LoadMainClass(env, mode, what);
	CHECK_EXCEPTION_NULL_LEAVE(mainClass);
	/*
	 * The LoadMainClass not only loads the main class, it will also ensure
	 * that the main method's signature is correct, therefore further checking
	 * is not required. The main method is invoked here so that extraneous java
	 * stacks are not in the application stack trace.
	 */
	mainID = (*env)->GetStaticMethodID(env, mainClass, "main",
									   "([Ljava/lang/String;)V");
	CHECK_EXCEPTION_NULL_LEAVE(mainID);

	/* Build platform specific argument array */
	mainArgs = CreateApplicationArgs(env, argv, argc);
	CHECK_EXCEPTION_NULL_LEAVE(mainArgs);

	/* Invoke main method. */
	if (args->printHelp) {
		jfieldID jyUsageField = (*env)->GetStaticFieldID(env, mainClass, "usage", "Ljava/lang/String;");
		jstring jyUsageText = (*env)->GetStaticObjectField(env, mainClass, jyUsageField);
		cstr_from_jstring(jyUsageTextC, jyUsageText);
		fputs(jyUsageTextC, stderr);
		puts("\n");
		print_help();
	} else {
		(*env)->CallStaticVoidMethod(env, mainClass, mainID, mainArgs);
	}

	/*
	 * The launcher's exit code (in the absence of calls to
	 * System.exit) will be non-zero if main threw an exception.
	 */
	ret = (*env)->ExceptionOccurred(env) == NULL ? 0 : 1;
	LEAVE();
}

/*
 * Checks the command line options to find which JVM type was
 * specified.  If no command line option was given for the JVM type,
 * the default type is used.  The environment variable
 * JDK_ALTERNATE_VM and the command line option -XXaltjvm= are also
 * checked as ways of specifying which JVM type to invoke.
 */
char *
CheckJvmType(int *pargc, char ***argv, jboolean speculative) {
	int i, argi;
	int argc;
	char **newArgv;
	int newArgvIdx = 0;
	int isVMType;
	int jvmidx = -1;
	char *jvmtype = getenv("JDK_ALTERNATE_VM");

	argc = *pargc;

	/* To make things simpler we always copy the argv array */
	newArgv = JLI_MemAlloc((argc + 1) * sizeof(char *));

	/* The program name is always present */
	//Was filtered away by jython.c, is now available in jysetup->progName
	//newArgv[newArgvIdx++] = (*argv)[0];

	for (argi = 1; argi < argc; argi++) {
		char *arg = (*argv)[argi];
		isVMType = 0;

		if (IsJavaArgs()) {
			if (arg[0] != '-') {
				newArgv[newArgvIdx++] = arg;
				continue;
			}
		} else {
			if (JLI_StrCmp(arg, "-classpath") == 0 ||
				JLI_StrCmp(arg, "-cp") == 0) {
				newArgv[newArgvIdx++] = arg;
				argi++;
				if (argi < argc) {
					newArgv[newArgvIdx++] = (*argv)[argi];
				}
				continue;
			}
			if (arg[0] != '-') break;
		}

		/* Did the user pass an explicit VM type? */
		i = KnownVMIndex(arg);
		if (i >= 0) {
			jvmtype = knownVMs[jvmidx = i].name + 1; /* skip the - */
			isVMType = 1;
			*pargc = *pargc - 1;
		}

		/* Did the user specify an "alternate" VM? */
		else if (JLI_StrCCmp(arg, "-XXaltjvm=") == 0 || JLI_StrCCmp(arg, "-J-XXaltjvm=") == 0) {
			isVMType = 1;
			jvmtype = arg+((arg[1]=='X')? 10 : 12);
			jvmidx = -1;
		}

		if (!isVMType) {
			newArgv[newArgvIdx++] = arg;
		}
	}

	/*
	 * Finish copying the arguments if we aborted the above loop.
	 * NOTE that if we aborted via "break" then we did NOT copy the
	 * last argument above, and in addition argi will be less than
	 * argc.
	 */
	while (argi < argc) {
		newArgv[newArgvIdx++] = (*argv)[argi];
		argi++;
	}

	/* argv is null-terminated */
	newArgv[newArgvIdx] = 0;

	/* Copy back argv */
	*argv = newArgv;
	*pargc = newArgvIdx;

	/* use the default VM type if not specified (no alias processing) */
	if (jvmtype == NULL) {
	  char* result = knownVMs[0].name+1;
	  /* Use a different VM type if we are on a server class machine? */
	  if ((knownVMs[0].flag == VM_IF_SERVER_CLASS) &&
		  (ServerClassMachine() == JNI_TRUE)) {
		result = knownVMs[0].server_class+1;
	  }
	  JLI_TraceLauncher("Default VM: %s\n", result);
	  return result;
	}

	/* if using an alternate VM, no alias processing */
	if (jvmidx < 0)
	  return jvmtype;

	/* Resolve aliases first */
	{
	  int loopCount = 0;
	  while (knownVMs[jvmidx].flag == VM_ALIASED_TO) {
		int nextIdx = KnownVMIndex(knownVMs[jvmidx].alias);

		if (loopCount > knownVMsCount) {
		  if (!speculative) {
			JLI_ReportErrorMessage(CFG_ERROR1);
			exit(1);
		  } else {
			return "ERROR";
			/* break; */
		  }
		}

		if (nextIdx < 0) {
		  if (!speculative) {
			JLI_ReportErrorMessage(CFG_ERROR2, knownVMs[jvmidx].alias);
			exit(1);
		  } else {
			return "ERROR";
		  }
		}
		jvmidx = nextIdx;
		jvmtype = knownVMs[jvmidx].name+1;
		loopCount++;
	  }
	}

	switch (knownVMs[jvmidx].flag) {
	case VM_WARN:
		if (!speculative) {
			JLI_ReportErrorMessage(CFG_WARN1, jvmtype, knownVMs[0].name + 1);
		}
		/* fall through */
	case VM_IGNORE:
		jvmtype = knownVMs[jvmidx=0].name + 1;
		/* fall through */
	case VM_KNOWN:
		break;
	case VM_ERROR:
		if (!speculative) {
			JLI_ReportErrorMessage(CFG_ERROR3, jvmtype);
			exit(1);
		} else {
			return "ERROR";
		}
	}

	return jvmtype;
}

/* copied from HotSpot function "atomll()" */
static int
parse_size(const char *s, jlong *result) {
  jlong n = 0;
  int args_read = sscanf(s, jlong_format_specifier(), &n);
  if (args_read != 1) {
	return 0;
  }
  while (*s != '\0' && *s >= '0' && *s <= '9') {
	s++;
  }
  // 4705540: illegal if more characters are found after the first non-digit
  if (JLI_StrLen(s) > 1) {
	return 0;
  }
  switch (*s) {
	case 'T': case 't':
	  *result = n * GB * KB;
	  return 1;
	case 'G': case 'g':
	  *result = n * GB;
	  return 1;
	case 'M': case 'm':
	  *result = n * MB;
	  return 1;
	case 'K': case 'k':
	  *result = n * KB;
	  return 1;
	case '\0':
	  *result = n;
	  return 1;
	default:
	  /* Create JVM with default stack and let VM handle malformed -Xss string*/
	  return 0;
  }
}

/*
 * Adds a new VM option with the given given name and value.
 */
void
AddOption(char *str, void *info)
{
//	puts("AddOption");
//	puts(str);
	/*
	 * Expand options array if needed to accommodate at least one more
	 * VM option.
	 */
	if (numOptions >= maxOptions) {
		if (options == 0) {
			maxOptions = 4;
			options = JLI_MemAlloc(maxOptions * sizeof(JavaVMOption));
		} else {
			JavaVMOption *tmp;
			maxOptions *= 2;
			tmp = JLI_MemAlloc(maxOptions * sizeof(JavaVMOption));
			memcpy(tmp, options, numOptions * sizeof(JavaVMOption));
			JLI_MemFree(options);
			options = tmp;
		}
	}
	options[numOptions].optionString = str;
	options[numOptions++].extraInfo = info;

	if (JLI_StrCCmp(str, "-Xss") == 0) {
		jlong tmp;
		if (parse_size(str + 4, &tmp)) {
			threadStackSize = tmp;
		}
	}

	if (JLI_StrCCmp(str, "-Xmx") == 0) {
		jlong tmp;
		if (parse_size(str + 4, &tmp)) {
			maxHeapSize = tmp;
		}
	}

	if (JLI_StrCCmp(str, "-Xms") == 0) {
		jlong tmp;
		if (parse_size(str + 4, &tmp)) {
		   initialHeapSize = tmp;
		}
	}
}

#define cpOption "-Djava.class.path=%s"
#define cpBootOption "-Xbootclasspath/a:%s"
static void
SetClassPath(const char *s, jboolean expand, jboolean boot)
{
	char *def;
	const char *orig = s;
	int cpl = boot ? sizeof(cpBootOption) : sizeof(cpOption);
	/*
	 * usually we should not get a null pointer, but there are cases where
	 * we might just get one, in which case we simply ignore it, and let the
	 * caller deal with it
	 */
	if (s == NULL)
		return;
	if (expand)
		s = JLI_WildcardExpandClasspath(s);
	if (cpl - 2 + JLI_StrLen(s) < JLI_StrLen(s))
		// s is corrupted after wildcard expansion
		return;
	def = JLI_MemAlloc(cpl
					   - 2 /* strlen("%s") */
					   + JLI_StrLen(s));
	sprintf(def, boot ? cpBootOption : cpOption, s);
//	puts("SetClassPath");
//	puts(def);
	AddOption(def, NULL);
	if (s != orig)
		JLI_MemFree((char *) s);
}

/*
 * Parses command line arguments.  Returns JNI_FALSE if launcher
 * should exit without starting vm, returns JNI_TRUE if vm needs
 * to be started to process given options.  *pret (the launcher
 * process return value) is set to 0 for a normal exit.
 */
static jboolean
ParseArguments(int *pargc, char ***pargv,
			   int *pmode, char **pwhat,
			   int *pret, const char *jrepath)
{
	int argc = *pargc;
	char **argv = *pargv;
	int mode = LM_UNKNOWN;
	char *arg;

	*pret = 0;

//launch jdb:
//	/usr/local/java/jdk1.8.0_25/bin/java -cp /usr/local/java/jdk1.8.0_25/lib/tools.jar:/usr/local/java/jdk1.8.0_25/lib/sa-jdi.jar -ms8m com.sun.tools.example.debug.tty.TTY /classes

	while ((arg = *argv) != 0 && *arg == '-') {
		argv++; --argc;
//		if (JLI_StrCmp(arg, "-classpath") == 0 || JLI_StrCmp(arg, "-cp") == 0) {
//			ARG_CHECK (argc, ARG_ERROR1, arg);
//			SetClassPath(*argv);
//			mode = LM_CLASS;
//			argv++; --argc;
////		} else if (JLI_StrCmp(arg, "-jar") == 0) {
////			ARG_CHECK (argc, ARG_ERROR2, arg);
////			mode = LM_JAR;
////		} else if (JLI_StrCmp(arg, "-help") == 0 ||
////				   JLI_StrCmp(arg, "-h") == 0 ||
////				   JLI_StrCmp(arg, "-?") == 0) {
////			printUsage = JNI_TRUE;
////			return JNI_TRUE;
//		} else
		if (JLI_StrCmp(arg, "-version") == 0) {
			printVersion = JNI_TRUE;
			return JNI_TRUE;
		} else if (JLI_StrCmp(arg, "-showversion") == 0) {
			showVersion = JNI_TRUE;
		} else if (JLI_StrCmp(arg, "-X") == 0) {
			printXUsage = JNI_TRUE;
			return JNI_TRUE;
/*
 * The following case checks for -XshowSettings OR -XshowSetting:SUBOPT.
 * In the latter case, any SUBOPT value not recognized will default to "all"
 */
		} else if (JLI_StrCmp(arg, "-XshowSettings") == 0 ||
				JLI_StrCCmp(arg, "-XshowSettings:") == 0) {
			showSettings = arg;
		} else if (JLI_StrCmp(arg, "-Xdiag") == 0) {
			AddOption("-Dsun.java.launcher.diag=true", NULL);
///*
// * The following case provide backward compatibility with old-style
// * command line options.
// */
//		} else if (JLI_StrCmp(arg, "-fullversion") == 0) {
//			JLI_ReportMessage("%s full version \"%s\"", _launcher_name, GetFullVersion());
//			return JNI_FALSE;
//		} else if (JLI_StrCmp(arg, "-verbosegc") == 0) {
//			AddOption("-verbose:gc", NULL);
//		} else if (JLI_StrCmp(arg, "-t") == 0) {
//			AddOption("-Xt", NULL);
//		} else if (JLI_StrCmp(arg, "-tm") == 0) {
//			AddOption("-Xtm", NULL);
//		} else if (JLI_StrCmp(arg, "-debug") == 0) {
//			AddOption("-Xdebug", NULL);
//		} else if (JLI_StrCmp(arg, "-noclassgc") == 0) {
//			AddOption("-Xnoclassgc", NULL);
//		} else if (JLI_StrCmp(arg, "-Xfuture") == 0) {
//			AddOption("-Xverify:all", NULL);
//		} else if (JLI_StrCmp(arg, "-verify") == 0) {
//			AddOption("-Xverify:all", NULL);
//		} else if (JLI_StrCmp(arg, "-verifyremote") == 0) {
//			AddOption("-Xverify:remote", NULL);
//		} else if (JLI_StrCmp(arg, "-noverify") == 0) {
//			AddOption("-Xverify:none", NULL);
//		} else if (JLI_StrCCmp(arg, "-prof") == 0) {
//			char *p = arg + 5;
//			char *tmp = JLI_MemAlloc(JLI_StrLen(arg) + 50);
//			if (*p) {
//				sprintf(tmp, "-Xrunhprof:cpu=old,file=%s", p + 1);
//			} else {
//				sprintf(tmp, "-Xrunhprof:cpu=old,file=java.prof");
//			}
//			AddOption(tmp, NULL);
//		} else if (JLI_StrCCmp(arg, "-ss") == 0 ||
//				   JLI_StrCCmp(arg, "-oss") == 0 ||
//				   JLI_StrCCmp(arg, "-ms") == 0 ||
//				   JLI_StrCCmp(arg, "-mx") == 0) {
//			char *tmp = JLI_MemAlloc(JLI_StrLen(arg) + 6);
//			sprintf(tmp, "-X%s", arg + 1); /* skip '-' */
//			AddOption(tmp, NULL);
//		} else if (JLI_StrCmp(arg, "-checksource") == 0 ||
//				   JLI_StrCmp(arg, "-cs") == 0 ||
//				   JLI_StrCmp(arg, "-noasyncgc") == 0) {
//			/* No longer supported */
//			JLI_ReportErrorMessage(ARG_WARN, arg);
//		} else if (JLI_StrCCmp(arg, "-version:") == 0 ||
//				   JLI_StrCmp(arg, "-no-jre-restrict-search") == 0 ||
//				   JLI_StrCmp(arg, "-jre-restrict-search") == 0 ||
//				   JLI_StrCCmp(arg, "-splash:") == 0) {
//			; /* Ignore machine independent options already handled */
//		} else if (ProcessPlatformOption(arg)) {
//			; /* Processing of platform dependent options */
//		} else if (RemovableOption(arg)) {
//			; /* Do not pass option to vm. */
//		} else {
	//Todo: check for bootclasspath and call JLI_WildcardExpandClasspath on its value
//			AddOption(arg, NULL);
		}
	}

//	if (--argc >= 0) {
//		*pwhat = *argv++;
//	}
	*pwhat = jythonClassP;

//	if (*pwhat == NULL) {
//		*pret = 1;
//	} else if (mode == LM_UNKNOWN) {
//		/* default to LM_CLASS if -jar and -cp option are
//		 * not specified */
//		mode = LM_CLASS;
//	}

	if (argc >= 0) {
		*pargc = argc;
		*pargv = argv;
	}

	*pmode = mode;

	return JNI_TRUE;
}

/*
 * Initializes the Java Virtual Machine. Also frees options array when
 * finished.
 */
static jboolean
InitializeJVM(JavaVM **pvm, JNIEnv **penv, InvocationFunctions *ifn)
{
	JavaVMInitArgs args;
	jint r;

	memset(&args, 0, sizeof(args));
	args.version  = JNI_VERSION_1_2;
	args.nOptions = numOptions;
	args.options  = options;
	args.ignoreUnrecognized = JNI_FALSE;

	if (JLI_IsTraceLauncher()) {
		int i = 0;
		printf("JavaVM args:\n	");
		printf("version 0x%08lx, ", (long)args.version);
		printf("ignoreUnrecognized is %s, ",
			   args.ignoreUnrecognized ? "JNI_TRUE" : "JNI_FALSE");
		printf("nOptions is %ld\n", (long)args.nOptions);
		for (i = 0; i < numOptions; i++)
			printf("	option[%2d] = '%s'\n",
				   i, args.options[i].optionString);
	}

	r = ifn->CreateJavaVM(pvm, (void **)penv, &args);
	JLI_MemFree(options);
	return r == JNI_OK;
}

static jclass helperClass = NULL;
jclass
GetLauncherHelperClass(JNIEnv *env)
{
	if (helperClass == NULL) {
		NULL_CHECK0(helperClass = FindBootStrapClass(env,
				"sun/launcher/LauncherHelper"));
	}
	return helperClass;
}

static jmethodID makePlatformStringMID = NULL;
/*
 * Returns a new Java string object for the specified platform string.
 */
static jstring
NewPlatformString(JNIEnv *env, char *s)
{
	int len = (int)JLI_StrLen(s);
	jbyteArray ary;
	jclass cls = GetLauncherHelperClass(env);
	NULL_CHECK0(cls);
	if (s == NULL)
		return 0;

	ary = (*env)->NewByteArray(env, len);
	if (ary != 0) {
		jstring str = 0;
		(*env)->SetByteArrayRegion(env, ary, 0, len, (jbyte *)s);
		if (!(*env)->ExceptionOccurred(env)) {
			if (makePlatformStringMID == NULL) {
				NULL_CHECK0(makePlatformStringMID = (*env)->GetStaticMethodID(env,
						cls, "makePlatformString", "(Z[B)Ljava/lang/String;"));
			}
			str = (*env)->CallStaticObjectMethod(env, cls,
					makePlatformStringMID, USE_STDERR, ary);
			(*env)->DeleteLocalRef(env, ary);
			return str;
		}
	}
	return 0;
}

/*
 * Returns a new array of Java string objects for the specified
 * array of platform strings.
 */
jobjectArray
NewPlatformStringArray(JNIEnv *env, char **strv, int strc)
{
	jarray cls;
	jarray ary;
	int i;

	NULL_CHECK0(cls = FindBootStrapClass(env, "java/lang/String"));
	NULL_CHECK0(ary = (*env)->NewObjectArray(env, strc, cls, 0));
	for (i = 0; i < strc; i++) {
		jstring str = NewPlatformString(env, *strv++);
		NULL_CHECK0(str);
		(*env)->SetObjectArrayElement(env, ary, i, str);
		(*env)->DeleteLocalRef(env, str);
	}
	return ary;
}

/*
 * Loads a class and verifies that the main class is present and it is ok to
 * call it for more details refer to the java implementation.
 */
static jclass
LoadMainClass(JNIEnv *env, int mode, char *name)
{
	jclass result;
	jlong start, end;
	//Hard-coded for Jython
	result =  (*env)->FindClass(env, name);
	if (JLI_IsTraceLauncher()) {
		end   = CounterGet();
		printf("%ld micro seconds to load main class\n",
			   (long)(jint)Counter2Micros(end-start));
		printf("----%s----\n", JLDEBUG_ENV_ENTRY);
	}

	return result;
}

/*
 * For tools, convert command line args thus:
 *   javac -cp foo:foo/"*" -J-ms32m ...
 *   java -ms32m -cp JLI_WildcardExpandClasspath(foo:foo/"*") ...
 *
 * Takes 4 parameters, and returns the populated arguments
 */
//static void
//TranslateApplicationArgs(int jargc, const char **jargv, int *pargc, char ***pargv)
//{
//	int argc = *pargc;
//	char **argv = *pargv;
//	int nargc = argc + jargc;
//	char **nargv = JLI_MemAlloc((nargc + 1) * sizeof(char *));
//	int i;
//
//	*pargc = nargc;
//	*pargv = nargv;
//
//	/* Copy the VM arguments (i.e. prefixed with -J) */
//	for (i = 0; i < jargc; i++) {
//		const char *arg = jargv[i];
//		if (arg[0] == '-' && arg[1] == 'J') {
//			*nargv++ = ((arg + 2) == NULL) ? NULL : JLI_StringDup(arg + 2);
//		}
//	}
//
//	for (i = 0; i < argc; i++) {
//		char *arg = argv[i];
//		if (arg[0] == '-' && arg[1] == 'J') {
//			if (arg[2] == '\0') {
//				JLI_ReportErrorMessage(ARG_ERROR3);
//				exit(1);
//			}
//			*nargv++ = arg + 2;
//		}
//	}
//
//	/* Copy the rest of the arguments */
//	for (i = 0; i < jargc ; i++) {
//		const char *arg = jargv[i];
//		if (arg[0] != '-' || arg[1] != 'J') {
//			*nargv++ = (arg == NULL) ? NULL : JLI_StringDup(arg);
//		}
//	}
//	for (i = 0; i < argc; i++) {
//		char *arg = argv[i];
//		if (arg[0] == '-') {
//			if (arg[1] == 'J')
//				continue;
//			if (IsWildCardEnabled() && arg[1] == 'c'
//					&& (JLI_StrCmp(arg, "-cp") == 0 ||
//					JLI_StrCmp(arg, "-classpath") == 0)
//					&& i < argc - 1) {
//				*nargv++ = arg;
//				*nargv++ = (char *) JLI_WildcardExpandClasspath(argv[i+1]);
//				i++;
//				continue;
//			}
//		}
//		*nargv++ = arg;
//	}
//	*nargv = 0;
//}

/*
 * For our tools, we try to add 3 VM options:
 *	  -Denv.class.path=<envcp>
 *	  -Dapplication.home=<apphome>
 *	  -Djava.class.path=<appcp>
 * <envcp>   is the user's setting of CLASSPATH -- for instance the user
 *		   tells javac where to find binary classes through this environment
 *		   variable.  Notice that users will be able to compile against our
 *		   tools classes (sun.tools.javac.Main) only if they explicitly add
 *		   tools.jar to CLASSPATH.
 * <apphome> is the directory where the application is installed.
 * <appcp>   is the classpath to where our apps' classfiles are.
 */
static jboolean
AddApplicationOptions(int cpathc, const char **cpathv)
{
	char *envcp, *apphome;//, *appcp;
	char home[MAXPATHLEN]; /* application home */
//	char separator[] = { PATH_SEPARATOR, '\0' };
//	int size, i;

	{
		const char *s = getenv("CLASSPATH");
		if (s) {
			s = (char *) JLI_WildcardExpandClasspath(s);
			/* 40 for -Denv.class.path= */
			if (JLI_StrLen(s) + 40 > JLI_StrLen(s)) { // Safeguard from overflow
				envcp = (char *)JLI_MemAlloc(JLI_StrLen(s) + 40);
				sprintf(envcp, "-Denv.class.path=%s", s);
				AddOption(envcp, NULL);
			}
		}
	}

	if (!GetApplicationHome(home, sizeof(home))) {
//	if (!GetJavaHome(home, sizeof(home))) {
		JLI_ReportErrorMessage(CFG_ERROR5);
		return JNI_FALSE;
	}

	/* 40 for '-Dapplication.home=' */
	apphome = (char *)JLI_MemAlloc(JLI_StrLen(home) + 40);
	sprintf(apphome, "-Dapplication.home=%s", home);
	AddOption(apphome, NULL);

//	/* How big is the application's classpath? */
//	size = 40;								 /* 40: "-Djava.class.path=" */
//	for (i = 0; i < cpathc; i++) {
//		size += (int)JLI_StrLen(home) + (int)JLI_StrLen(cpathv[i]) + 1; /* 1: separator */
//	}
//	appcp = (char *)JLI_MemAlloc(size + 1);
//	JLI_StrCpy(appcp, "-Djava.class.path=");
//	for (i = 0; i < cpathc; i++) {
//		JLI_StrCat(appcp, home);       /* c:\program files\myapp */
//		JLI_StrCat(appcp, cpathv[i]);  /* \lib\myapp.jar         */
//		JLI_StrCat(appcp, separator);  /* ;                      */
//	}
//	appcp[JLI_StrLen(appcp)-1] = '\0';  /* remove trailing path separator */
//	AddOption(appcp, NULL);
	return JNI_TRUE;
}

/*
 * inject the -Dsun.java.command pseudo property into the args structure
 * this pseudo property is used in the HotSpot VM to expose the
 * Java class name and arguments to the main method to the VM. The
 * HotSpot VM uses this pseudo property to store the Java class name
 * (or jar file name) and the arguments to the class's main method
 * to the instrumentation memory region. The sun.java.command pseudo
 * property is not exported by HotSpot to the Java layer.
 */
void
SetJavaCommandLineProp(char *what, int argc, char **argv)
{

	int i = 0;
	size_t len = 0;
	char* javaCommand = NULL;
	char* dashDstr = "-Dsun.java.command=";

	if (what == NULL) {
		/* unexpected, one of these should be set. just return without
		 * setting the property
		 */
		return;
	}

	/* determine the amount of memory to allocate assuming
	 * the individual components will be space separated
	 */
	len = JLI_StrLen(what);
	for (i = 0; i < argc; i++) {
		len += JLI_StrLen(argv[i]) + 1;
	}

	/* allocate the memory */
	javaCommand = (char*) JLI_MemAlloc(len + JLI_StrLen(dashDstr) + 1);

	/* build the -D string */
	*javaCommand = '\0';
	JLI_StrCat(javaCommand, dashDstr);
	JLI_StrCat(javaCommand, what);

	for (i = 0; i < argc; i++) {
		/* the components of the string are space separated. In
		 * the case of embedded white space, the relationship of
		 * the white space separated components to their true
		 * positional arguments will be ambiguous. This issue may
		 * be addressed in a future release.
		 */
		JLI_StrCat(javaCommand, " ");
		JLI_StrCat(javaCommand, argv[i]);
	}

	AddOption(javaCommand, NULL);
}

/*
 * JVM would like to know if it's created by a standard Sun launcher, or by
 * user native application, the following property indicates the former.
 */
void
SetJavaLauncherProp() {
  AddOption("-Dsun.java.launcher=LIJY-LAUNCH", NULL);
}

/*
 * Prints the version information from the java.version and other properties.
 */
static void
PrintJavaVersion(JNIEnv *env, jboolean extraLF)
{
	jclass ver;
	jmethodID print;

	NULL_CHECK(ver = FindBootStrapClass(env, "sun/misc/Version"));
	NULL_CHECK(print = (*env)->GetStaticMethodID(env,
												 ver,
												 (extraLF == JNI_TRUE) ? "println" : "print",
												 "()V"
												 )
			  );

	(*env)->CallStaticVoidMethod(env, ver, print);
}

/*
 * Prints all the Java settings, see the java implementation for more details.
 */
static void
ShowSettings(JNIEnv *env, char *optString)
{
	jmethodID showSettingsID;
	jstring joptString;
	jclass cls = GetLauncherHelperClass(env);
	NULL_CHECK(cls);
	NULL_CHECK(showSettingsID = (*env)->GetStaticMethodID(env, cls,
			"showSettings", "(ZLjava/lang/String;JJJZ)V"));
	joptString = (*env)->NewStringUTF(env, optString);
	(*env)->CallStaticVoidMethod(env, cls, showSettingsID,
								 USE_STDERR,
								 joptString,
								 (jlong)initialHeapSize,
								 (jlong)maxHeapSize,
								 (jlong)threadStackSize,
								 ServerClassMachine());
}

/*
 * Prints default usage or the Xusage message, see sun.launcher.LauncherHelper.java
 */
static void
PrintUsage(JNIEnv* env, jboolean doXUsage)
{
  jmethodID initHelp, vmSelect, vmSynonym, vmErgo, printHelp, printXUsageMessage;
  jstring jprogname, vm1, vm2;
  int i;
  jclass cls = GetLauncherHelperClass(env);
  NULL_CHECK(cls);
  if (doXUsage) {
	NULL_CHECK(printXUsageMessage = (*env)->GetStaticMethodID(env, cls,
										"printXUsageMessage", "(Z)V"));
	(*env)->CallStaticVoidMethod(env, cls, printXUsageMessage, USE_STDERR);
  } else {
	NULL_CHECK(initHelp = (*env)->GetStaticMethodID(env, cls,
										"initHelpMessage", "(Ljava/lang/String;)V"));

	NULL_CHECK(vmSelect = (*env)->GetStaticMethodID(env, cls, "appendVmSelectMessage",
										"(Ljava/lang/String;Ljava/lang/String;)V"));

	NULL_CHECK(vmSynonym = (*env)->GetStaticMethodID(env, cls,
										"appendVmSynonymMessage",
										"(Ljava/lang/String;Ljava/lang/String;)V"));
	NULL_CHECK(vmErgo = (*env)->GetStaticMethodID(env, cls,
										"appendVmErgoMessage", "(ZLjava/lang/String;)V"));

	NULL_CHECK(printHelp = (*env)->GetStaticMethodID(env, cls,
										"printHelpMessage", "(Z)V"));

	jprogname = (*env)->NewStringUTF(env, _program_name);

	/* Initialize the usage message with the usual preamble */
	(*env)->CallStaticVoidMethod(env, cls, initHelp, jprogname);


	/* Assemble the other variant part of the usage */
	if ((knownVMs[0].flag == VM_KNOWN) ||
		(knownVMs[0].flag == VM_IF_SERVER_CLASS)) {
	  vm1 = (*env)->NewStringUTF(env, knownVMs[0].name);
	  vm2 =  (*env)->NewStringUTF(env, knownVMs[0].name+1);
	  (*env)->CallStaticVoidMethod(env, cls, vmSelect, vm1, vm2);
	}
	for (i=1; i<knownVMsCount; i++) {
	  if (knownVMs[i].flag == VM_KNOWN) {
		vm1 =  (*env)->NewStringUTF(env, knownVMs[i].name);
		vm2 =  (*env)->NewStringUTF(env, knownVMs[i].name+1);
		(*env)->CallStaticVoidMethod(env, cls, vmSelect, vm1, vm2);
	  }
	}
	for (i=1; i<knownVMsCount; i++) {
	  if (knownVMs[i].flag == VM_ALIASED_TO) {
		vm1 =  (*env)->NewStringUTF(env, knownVMs[i].name);
		vm2 =  (*env)->NewStringUTF(env, knownVMs[i].alias+1);
		(*env)->CallStaticVoidMethod(env, cls, vmSynonym, vm1, vm2);
	  }
	}

	/* The first known VM is the default */
	{
	  jboolean isServerClassMachine = ServerClassMachine();

	  const char* defaultVM  =  knownVMs[0].name+1;
	  if ((knownVMs[0].flag == VM_IF_SERVER_CLASS) && isServerClassMachine) {
		defaultVM = knownVMs[0].server_class+1;
	  }

	  vm1 =  (*env)->NewStringUTF(env, defaultVM);
	  (*env)->CallStaticVoidMethod(env, cls, vmErgo, isServerClassMachine,  vm1);
	}

	/* Complete the usage message and print to stderr*/
	(*env)->CallStaticVoidMethod(env, cls, printHelp, USE_STDERR);
  }
  return;
}

/*
 * Read the jvm.cfg file and fill the knownJVMs[] array.
 *
 * The functionality of the jvm.cfg file is subject to change without
 * notice and the mechanism will be removed in the future.
 *
 * The lexical structure of the jvm.cfg file is as follows:
 *
 *	 jvmcfg		 :=  { vmLine }
 *	 vmLine		 :=  knownLine
 *					|   aliasLine
 *					|   warnLine
 *					|   ignoreLine
 *					|   errorLine
 *					|   predicateLine
 *					|   commentLine
 *	 knownLine	  :=  flag  "KNOWN"				  EOL
 *	 warnLine	   :=  flag  "WARN"				   EOL
 *	 ignoreLine	 :=  flag  "IGNORE"				 EOL
 *	 errorLine	  :=  flag  "ERROR"				  EOL
 *	 aliasLine	  :=  flag  "ALIASED_TO"	   flag  EOL
 *	 predicateLine  :=  flag  "IF_SERVER_CLASS"  flag  EOL
 *	 commentLine	:=  "#" text					   EOL
 *	 flag		   :=  "-" identifier
 *
 * The semantics are that when someone specifies a flag on the command line:
 * - if the flag appears on a knownLine, then the identifier is used as
 *   the name of the directory holding the JVM library (the name of the JVM).
 * - if the flag appears as the first flag on an aliasLine, the identifier
 *   of the second flag is used as the name of the JVM.
 * - if the flag appears on a warnLine, the identifier is used as the
 *   name of the JVM, but a warning is generated.
 * - if the flag appears on an ignoreLine, the identifier is recognized as the
 *   name of a JVM, but the identifier is ignored and the default vm used
 * - if the flag appears on an errorLine, an error is generated.
 * - if the flag appears as the first flag on a predicateLine, and
 *   the machine on which you are running passes the predicate indicated,
 *   then the identifier of the second flag is used as the name of the JVM,
 *   otherwise the identifier of the first flag is used as the name of the JVM.
 * If no flag is given on the command line, the first vmLine of the jvm.cfg
 * file determines the name of the JVM.
 * PredicateLines are only interpreted on first vmLine of a jvm.cfg file,
 * since they only make sense if someone hasn't specified the name of the
 * JVM on the command line.
 *
 * The intent of the jvm.cfg file is to allow several JVM libraries to
 * be installed in different subdirectories of a single JRE installation,
 * for space-savings and convenience in testing.
 * The intent is explicitly not to provide a full aliasing or predicate
 * mechanism.
 */
jint
ReadKnownVMs(const char *jvmCfgName, jboolean speculative)
{
	FILE *jvmCfg;
	char line[MAXPATHLEN+20];
	int cnt = 0;
	int lineno = 0;
	jlong start, end;
	int vmType;
	char *tmpPtr;
	char *altVMName = NULL;
	char *serverClassVMName = NULL;
	static char *whiteSpace = " \t";
	if (JLI_IsTraceLauncher()) {
		start = CounterGet();
	}

	jvmCfg = fopen(jvmCfgName, "r");
	if (jvmCfg == NULL) {
	  if (!speculative) {
		JLI_ReportErrorMessage(CFG_ERROR6, jvmCfgName);
		exit(1);
	  } else {
		return -1;
	  }
	}
	while (fgets(line, sizeof(line), jvmCfg) != NULL) {
		vmType = VM_UNKNOWN;
		lineno++;
		if (line[0] == '#')
			continue;
		if (line[0] != '-') {
			JLI_ReportErrorMessage(CFG_WARN2, lineno, jvmCfgName);
		}
		if (cnt >= knownVMsLimit) {
			GrowKnownVMs(cnt);
		}
		line[JLI_StrLen(line)-1] = '\0'; /* remove trailing newline */
		tmpPtr = line + JLI_StrCSpn(line, whiteSpace);
		if (*tmpPtr == 0) {
			JLI_ReportErrorMessage(CFG_WARN3, lineno, jvmCfgName);
		} else {
			/* Null-terminate this string for JLI_StringDup below */
			*tmpPtr++ = 0;
			tmpPtr += JLI_StrSpn(tmpPtr, whiteSpace);
			if (*tmpPtr == 0) {
				JLI_ReportErrorMessage(CFG_WARN3, lineno, jvmCfgName);
			} else {
				if (!JLI_StrCCmp(tmpPtr, "KNOWN")) {
					vmType = VM_KNOWN;
				} else if (!JLI_StrCCmp(tmpPtr, "ALIASED_TO")) {
					tmpPtr += JLI_StrCSpn(tmpPtr, whiteSpace);
					if (*tmpPtr != 0) {
						tmpPtr += JLI_StrSpn(tmpPtr, whiteSpace);
					}
					if (*tmpPtr == 0) {
						JLI_ReportErrorMessage(CFG_WARN3, lineno, jvmCfgName);
					} else {
						/* Null terminate altVMName */
						altVMName = tmpPtr;
						tmpPtr += JLI_StrCSpn(tmpPtr, whiteSpace);
						*tmpPtr = 0;
						vmType = VM_ALIASED_TO;
					}
				} else if (!JLI_StrCCmp(tmpPtr, "WARN")) {
					vmType = VM_WARN;
				} else if (!JLI_StrCCmp(tmpPtr, "IGNORE")) {
					vmType = VM_IGNORE;
				} else if (!JLI_StrCCmp(tmpPtr, "ERROR")) {
					vmType = VM_ERROR;
				} else if (!JLI_StrCCmp(tmpPtr, "IF_SERVER_CLASS")) {
					tmpPtr += JLI_StrCSpn(tmpPtr, whiteSpace);
					if (*tmpPtr != 0) {
						tmpPtr += JLI_StrSpn(tmpPtr, whiteSpace);
					}
					if (*tmpPtr == 0) {
						JLI_ReportErrorMessage(CFG_WARN4, lineno, jvmCfgName);
					} else {
						/* Null terminate server class VM name */
						serverClassVMName = tmpPtr;
						tmpPtr += JLI_StrCSpn(tmpPtr, whiteSpace);
						*tmpPtr = 0;
						vmType = VM_IF_SERVER_CLASS;
					}
				} else {
					JLI_ReportErrorMessage(CFG_WARN5, lineno, &jvmCfgName[0]);
					vmType = VM_KNOWN;
				}
			}
		}

		JLI_TraceLauncher("jvm.cfg[%d] = ->%s<-\n", cnt, line);
		if (vmType != VM_UNKNOWN) {
			knownVMs[cnt].name = JLI_StringDup(line);
			knownVMs[cnt].flag = vmType;
			switch (vmType) {
			default:
				break;
			case VM_ALIASED_TO:
				knownVMs[cnt].alias = JLI_StringDup(altVMName);
				JLI_TraceLauncher("	name: %s  vmType: %s  alias: %s\n",
				   knownVMs[cnt].name, "VM_ALIASED_TO", knownVMs[cnt].alias);
				break;
			case VM_IF_SERVER_CLASS:
				knownVMs[cnt].server_class = JLI_StringDup(serverClassVMName);
				JLI_TraceLauncher("	name: %s  vmType: %s  server_class: %s\n",
					knownVMs[cnt].name, "VM_IF_SERVER_CLASS", knownVMs[cnt].server_class);
				break;
			}
			cnt++;
		}
	}
	fclose(jvmCfg);
	knownVMsCount = cnt;

	if (JLI_IsTraceLauncher()) {
		end   = CounterGet();
		printf("%ld micro seconds to parse jvm.cfg\n",
			   (long)(jint)Counter2Micros(end-start));
	}

	return cnt;
}


static void
GrowKnownVMs(int minimum)
{
	struct vmdesc* newKnownVMs;
	int newMax;

	newMax = (knownVMsLimit == 0 ? INIT_MAX_KNOWN_VMS : (2 * knownVMsLimit));
	if (newMax <= minimum) {
		newMax = minimum;
	}
	newKnownVMs = (struct vmdesc*) JLI_MemAlloc(newMax * sizeof(struct vmdesc));
	if (knownVMs != NULL) {
		memcpy(newKnownVMs, knownVMs, knownVMsLimit * sizeof(struct vmdesc));
	}
	JLI_MemFree(knownVMs);
	knownVMs = newKnownVMs;
	knownVMsLimit = newMax;
}


/* Returns index of VM or -1 if not found */
static int
KnownVMIndex(const char* name)
{
	int i;
	if (JLI_StrCCmp(name, "-J") == 0) name += 2;
	for (i = 0; i < knownVMsCount; i++) {
		if (!JLI_StrCmp(name, knownVMs[i].name)) {
			return i;
		}
	}
	return -1;
}

static void
FreeKnownVMs()
{
	int i;
	for (i = 0; i < knownVMsCount; i++) {
		JLI_MemFree(knownVMs[i].name);
		knownVMs[i].name = NULL;
	}
	JLI_MemFree(knownVMs);
}

const char*
GetProgramName()
{
	return _program_name;
}

const char*
GetLauncherName()
{
	return _launcher_name;
}

jint
GetErgoPolicy()
{
	return _ergo_policy;
}

//Added by hand
jboolean
ServerClassMachine() {
	return (GetErgoPolicy() == ALWAYS_SERVER_CLASS) ? JNI_TRUE : JNI_FALSE;
}

jboolean
IsJavaArgs()
{
	return _is_java_args;
}

static jboolean
IsWildCardEnabled()
{
	return _wc_enabled;
}

int
ContinueInNewThread(InvocationFunctions* ifn, jlong threadStackSize,
					int argc, char **argv,
					int mode, char *what, int ret, jboolean help)
{
	/*
	 * If user doesn't specify stack size, check if VM has a preference.
	 * Note that HotSpot no longer supports JNI_VERSION_1_1 but it will
	 * return its default stack size through the init args structure.
	 */
	if (threadStackSize == 0) {
	  struct JDK1_1InitArgs args1_1;
	  memset((void*)&args1_1, 0, sizeof(args1_1));
	  args1_1.version = JNI_VERSION_1_1;
	  ifn->GetDefaultJavaVMInitArgs(&args1_1);  /* ignore return value */
	  if (args1_1.javaStackSize > 0) {
		 threadStackSize = args1_1.javaStackSize;
	  }
	}
	{ /* Create a new thread to create JVM and invoke main method */
	  JavaMainArgs args;
	  int rslt;

	  args.argc = argc;
	  args.argv = argv;
	  args.mode = mode;
	  args.what = what;
	  args.ifn = *ifn;
	  args.printHelp = help;

	  rslt = ContinueInNewThread0(JavaMain, threadStackSize, (void*)&args);
	  /* If the caller has deemed there is an error we
	   * simply return that, otherwise we return the value of
	   * the callee
	   */
	  return (ret != 0) ? ret : rslt;
	}
}

static void
DumpState()
{
	if (!JLI_IsTraceLauncher()) return ;
	printf("Launcher state:\n");
	printf("\tdebug:%s\n", (JLI_IsTraceLauncher() == JNI_TRUE) ? "on" : "off");
	printf("\tjavargs:%s\n", (_is_java_args == JNI_TRUE) ? "on" : "off");
	printf("\tprogram name:%s\n", GetProgramName());
	printf("\tlauncher name:%s\n", GetLauncherName());
	printf("\tjavaw:%s\n", (IsJavaw() == JNI_TRUE) ? "on" : "off");
	printf("\tergo_policy:");
	switch(GetErgoPolicy()) {
		case NEVER_SERVER_CLASS:
			printf("NEVER_ACT_AS_A_SERVER_CLASS_MACHINE\n");
			break;
		case ALWAYS_SERVER_CLASS:
			printf("ALWAYS_ACT_AS_A_SERVER_CLASS_MACHINE\n");
			break;
		default:
			printf("DEFAULT_ERGONOMICS_POLICY\n");
	}
}

/*
 * Return JNI_TRUE for an option string that has no effect but should
 * _not_ be passed on to the vm; return JNI_FALSE otherwise.  On
 * Solaris SPARC, this screening needs to be done if:
 *	-d32 or -d64 is passed to a binary with an unmatched data model
 *	(the exec in CreateExecutionEnvironment removes -d<n> options and points the
 *	exec to the proper binary).  In the case of when the data model and the
 *	requested version is matched, an exec would not occur, and these options
 *	were erroneously passed to the vm.
 */
jboolean
RemovableOption(char * option)
{
  /*
   * Unconditionally remove both -d32 and -d64 options since only
   * the last such options has an effect; e.g.
   * java -d32 -d64 -d32 -version
   * is equivalent to
   * java -d32 -version
   */

  if( (JLI_StrCCmp(option, "-d32")  == 0 ) ||
	  (JLI_StrCCmp(option, "-d64")  == 0 ) )
	return JNI_TRUE;
  else
	return JNI_FALSE;
}

/*
 * A utility procedure to always print to stderr
 */
void
JLI_ReportMessage(const char* fmt, ...)
{
	va_list vl;
	va_start(vl, fmt);
	vfprintf(stderr, fmt, vl);
	fprintf(stderr, "\n");
	va_end(vl);
}
