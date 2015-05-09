/*
 * jython.c
 *
 *  Created on: 26.04.2015
 *	  Author: Stefan Richthofer
 *
 * This file contains Jython-specific parameter-logic for LiJy-launch.
 * It is roughly a port of the Jython launch-script to C.
 */

#include "jython.h"

#ifdef _WIN32
#include <io.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif
//is_windows = os.name == "nt" or (os.name == "java" and os._name == "nt")

/* The caller is responsible to free the resulting pointer
 * by calling freeSetup after using it.
 */
JySetup* parse_launcher_args(int argc, char** args) {
	JySetup* result = malloc(sizeof(JySetup));
	result->boot = JNI_FALSE;
	result->jdb = JNI_FALSE;
	result->help = JNI_FALSE;
	result->print_requested = JNI_FALSE;
	result->profile = JNI_FALSE;
	result->tty = JNI_FALSE;
	result->pythonHomeInArgs = JNI_FALSE;
	result->unameInArgs = JNI_FALSE;
	result->executableInArgs = JNI_FALSE;
	result->ttyInArgs = JNI_FALSE;
	result->consoleInArgs = JNI_FALSE;
	result->propCount = 0;
	result->javaCount = 0;
	result->jythonCount = 0;
	//result->propValues = NULL;
	//result->propKeys = NULL;
	result->properties = NULL;
	result->java = NULL;
	result->jython = NULL;
	result->cp = NULL;
	result->mem = NULL;
	result->stack = NULL;
	result->uname = NULL;
	setString0(result, progName, args[0]);
	int argOff = 1;
	char* tmp[argc];
	int tmpPos = 0;
	int i;
	for (i = 1; i < argc; ++i) {
		if (strncmp(args[i], "-D", 2) == 0) {
			checkProperty(args[i], executableOpt, result->executableInArgs)
			checkProperty(args[i], unameOpt, result->unameInArgs)
//			if (strncmp(args[i]+2, unameOpt+2, sizeof(unameOpt)-3) == 0) {
//				result->unameInArgs = JNI_TRUE;
//				char* unme = strchr(args[i]+2, '=');
//				result->uname = malloc((strlen(unme)-1)*sizeof(char));
//				strcpy(result->uname, unme+1);
//			}
			checkProperty(args[i], homeOpt, result->pythonHomeInArgs)
			checkProperty(args[i], ttyOpt, result->ttyInArgs)
			checkProperty(args[i], consoleOpt, result->consoleInArgs)
			result->propCount++;
			tmp[tmpPos++] = args[i];
			argOff++;
		} else if (strcmp(args[i], "-J-classpath") == 0
				|| strcmp(args[i], "-J-cp") == 0) {
			if (i+1 < argc) {
				++i;
			} else {
				bad_option("Argument expected for -J-classpath option");
			}
			if (strncmp(args[i], "-", 1) == 0) {
				bad_option("Bad option for -J-classpath");
			} else {
				setString(result, cp, args[i]);
			}
			argOff += 2;
		} else if (strncmp(args[i], "-J-Xmx", 6) == 0) {
			setString(result, mem, args[i]+2);
			argOff++;
		} else if (strncmp(args[i], "-J-Xss", 6) == 0) {
			setString(result, stack, args[i]+2);
			argOff++;
		} else if (strncmp(args[i], "-J", 2) == 0) {
			result->javaCount++;
			tmp[tmpPos++] = args[i];
			argOff++;
		} else if (strcmp(args[i], "--print") == 0) {
			result->print_requested = JNI_TRUE;
			argOff++;
		} else if (strcmp(args[i], "-h") == 0
				|| strcmp(args[i], "--help") == 0) {
			result->help = JNI_TRUE;
			argOff++;
		} else if (strcmp(args[i], "--boot") == 0) {
			result->boot = JNI_TRUE;
			argOff++;
		} else if (strcmp(args[i], "--jdb") == 0) {
			result->jdb = JNI_TRUE;
			argOff++;
		} else if (strcmp(args[i], "--profile") == 0) {
			result->profile = JNI_TRUE;
			argOff++;
		} else if (strncmp(args[i], "--", 2) == 0) {
			//pass these args on to jython
			result->jythonCount++;
			tmp[tmpPos++] = args[i];
			argOff++;
		} else {
			break;
		}
	}
	//remaining argcount is argc-argOff, so add this to jythonCount
	result->jythonCount += argc-argOff;
	result->jython = malloc(result->jythonCount*sizeof(char*));
//	result->propKeys = malloc(result->propCount*sizeof(char*));
//	result->propValues = malloc(result->propCount*sizeof(char*));
	result->properties = malloc(result->propCount*sizeof(char*));
	result->java = malloc(result->javaCount*sizeof(char*));
	int javaPos = 0;
	int propPos = 0;
	int jythonPos = 0;
	for (i = 0; i < tmpPos; ++i) {
		if (strncmp(tmp[i], "-D", 2) == 0) {
//			char* v = strchr(tmp[i]+2, '=');
//			if (v) {
//				setString0(result, propValues[propPos], v+1);
//				result->propKeys[propPos] = malloc(v-tmp[i]+2);
//				strncpy(result->propKeys[propPos++], tmp[i]+2, v-tmp[i]+2);
//			} else {
//				bad_option("Bad option for -D: missing '='");
//			}
			setString0(result, properties[propPos], tmp[i]);
			propPos++;
		}
		else if (strncmp(tmp[i], "-J", 2) == 0) {
			setString0(result, java[javaPos], tmp[i]+2);
			javaPos++;
		}
		else if (strncmp(tmp[i], "--", 2) == 0) {
			setString0(result, jython[jythonPos], tmp[i]);
			jythonPos++;
		}
	}
	for (; argOff < argc; ++argOff) {
		setString0(result, jython[jythonPos], args[argOff]);
		jythonPos++;
	}

	if (!result->cp) {
		char* tmp = getenv("CLASSPATH");
		if (tmp) {
			setString0(result, cp, tmp);
		} else {
			setString0(result, cp, ".");
		}
	}
	if (!result->unameInArgs) {
		//obtain uname...
#ifdef _WIN32
		char* uname = "windows";
		setString0(result, uname, uname);
#else
		struct utsname unameResult;
		uname(&unameResult);
		if (strncmp(unameResult.sysname, "cygwin", sizeof("cygwin")-1) == 0) {
			setString0(result, uname, "cygwin");
		} else {
			setString0(result, uname, unameResult.sysname);
			char* p = result->uname;
			for ( ; *p; ++p) *p = tolower(*p);
		}
#endif
	}
	if (!result->ttyInArgs) {
		//obtain tty...
#ifdef _WIN32
		result->tty = _isatty(_fileno(stdin));
#else
		result->tty = isatty(fileno(stdin));
#endif
	}
	if (!result->mem) {
		char* tmp = getenv("JAVA_MEM");
		if (tmp) {
			setString0(result, mem, tmp);
		} else {
			setString0(result, mem, defaultMem);
		}
	}
	if (!result->stack) {
		char* tmp = getenv("JAVA_STACK");
		if (tmp) {
			setString0(result, stack, tmp);
		} else {
			setString0(result, stack, defaultStack);
		}
	}
	return result;//, args[i:]

	//		args = [self.java_command]
	//		args.extend(self.java_opts)
	//		args.extend(self.args.java)
	//
	//		classpath = self.java_classpath
	//		jython_jars = self.jython_jars
	//		if self.args.boot:
	//			args.append("-Xbootclasspath/a:%s" % self.convert_path(self.make_classpath(jython_jars)))
	//		else:
	//			classpath = self.make_classpath(jython_jars) + self.classpath_delimiter + classpath
	//		args.extend(["-classpath", self.convert_path(classpath)])
	//
	//		if "python.home" not in self.args.properties:
	//			args.append("-Dpython.home=%s" % self.convert_path(self.jython_home))
	//		if "python.executable" not in self.args.properties:
	//			args.append("-Dpython.executable=%s" % self.convert_path(self.executable))
	//		if "python.launcher.uname" not in self.args.properties:
	//			args.append("-Dpython.launcher.uname=%s" % self.uname)
	//		// Determines whether running on a tty for the benefit of
	//		// running on Cygwin. This step is needed because the Mintty
	//		// terminal emulator doesn't behave like a standard Microsoft
	//		// Windows tty, and so JNR Posix doesn't detect it properly.
	//		if "python.launcher.tty" not in self.args.properties:
	//			args.append("-Dpython.launcher.tty=%s" % str(os.isatty(sys.stdin.fileno())).lower())
	//		if self.uname == "cygwin" and "python.console" not in self.args.properties:
	//			args.append("-Dpython.console=org.python.core.PlainConsole")
	//		if self.args.profile:
	//			args.append("-XX:-UseSplitVerifier")
	//			args.append("-javaagent:%s" % self.convert_path(self.java_profile_agent))
	//		for k, v in self.args.properties.iteritems():
	//			args.append("-D%s=%s" % (self.convert(k), self.convert(v)))
	//		args.append("org.python.util.jython")
	//		if self.args.help:
	//			args.append("--help")
	//		args.extend(self.jython_args)
}

void freeSetup(JySetup* setup) {
	int i;
	if (setup->jython) {
		for (i = 0; i < setup->jythonCount; ++i) {
			free(setup->jython[i]);
		}
		free(setup->jython);
	}
	if (setup->java) {
		for (i = 0; i < setup->javaCount; ++i) {
			free(setup->java[i]);
		}
		free(setup->java);
	}
//	if (setup->propKeys) {
//		for (i = 0; i < setup->propCount; ++i) {
//			free(setup->propKeys[i]);
//		}
//		free(setup->propKeys);
//	}
//	if (setup->propValues) {
//		for (i = 0; i < setup->propCount; ++i) {
//			free(setup->propValues[i]);
//		}
//		free(setup->propValues);
//	}
	if (setup->properties) {
		for (i = 0; i < setup->propCount; ++i) {
			free(setup->properties[i]);
		}
		free(setup->properties);
	}
	free(setup->progName);
	//if (setup->cp)
		free(setup->cp);
	//if (setup->mem)
		free(setup->mem);
	//if (setup->stack)
		free(setup->stack);
	if (setup->uname)
		free(setup->uname);
	free(setup);
}

#define printBool(js, name) \
	if (js->name) printf("%s: true\n", #name); \
	else printf("%s: false\n", #name)

void printSetup(JySetup* js) {
	printf("progName: %s\n", js->progName);
	printBool(js, boot);
	printBool(js, jdb);
	printBool(js, help);
	printBool(js, print_requested);
	printBool(js, profile);
	printf("classpath: %s\n", js->cp);
	printf("mem: %s\n", js->mem);
	printf("stack: %s\n", js->stack);
	//printf("app-arg offset: %i\n", js->argOff);
	int i;
	puts("jython args:");
	for (i = 0; i < js->jythonCount; ++i) {
		printf("    %s\n", js->jython[i]);
	}
	puts("properties:");
//	for (i = 0; i < js->propCount; ++i) {
//		printf("    %s = %s\n", js->propKeys[i], js->propValues[i]);
//	}
	for (i = 0; i < js->propCount; ++i) {
		printf("    %s\n", js->properties[i]);
	}
	puts("-J-pass-through:");
	for (i = 0; i < js->javaCount; ++i) {
		printf("    %s\n", js->java[i]);
	}
	puts("");
}

//Caller is responsible to call free on return value.
#define cygpathCmd "cygpath --windows "
int cygpathCall(char* path, char* dest) {
	char* cmd[sizeof(cygpathCmd)+strlen(path)];
	strcpy(cmd, cygpathCmd);
	strcpy(cmd+sizeof(cygpathCmd), path);
	FILE* fp = popen(cmd, "r");
	if (!fp) return -1;
	fgets(dest, PATH_MAX, fp);
	if (pclose(fp) == -1) return -1;
	return 0;
}

//class JythonCommand(object):
//
//	def __init__(self, args, jython_args):
//		self.args = args
//		self.jython_args = jython_args
//
//	@property
//	def uname(self):
//		if hasattr(self, "_uname"):
//			return self._uname
//		if is_windows:
//			self._uname = "windows"
//		else:
//			uname = subprocess.check_output(["uname"]).strip().lower()
//			if uname.startswith("cygwin"):
//				self._uname = "cygwin"
//			else:
//				self._uname = uname
//		return self._uname
//
//	@property
//	def java_home(self):
//		if not hasattr(self, "_java_home"):
//			self.setup_java_command()
//		return self._java_home
//
//	@property
//	def java_command(self):
//		if not hasattr(self, "_java_command"):
//			self.setup_java_command()
//		return self._java_command
//
//	def setup_java_command(self):
//		if self.args.help:
//			self._java_home = None
//			self._java_command = "java"
//			return
//
//		if "JAVA_HOME" not in os.environ:
//			self._java_home = None
//			self._java_command = "jdb" if self.args.jdb else "java"
//		else:
//			self._java_home = os.environ["JAVA_HOME"]
//			if self.uname == "cygwin":
//				self._java_command = "jdb" if self.args.jdb else "java"
//			else:
//				self._java_command = os.path.join(
//					self.java_home, "bin",
//					"jdb" if self.args.jdb else "java")
//
//	@property
//	def executable(self):
//		"""Path to executable"""
//		if hasattr(self, "_executable"):
//			return self._executable
//		// Modified from
//		// http://stackoverflow.com/questions/3718657/how-to-properly-determine-current-script-directory-in-python/22881871#22881871
//		if getattr(sys, "frozen", False): // py2exe, PyInstaller, cx_Freeze
//			path = os.path.abspath(sys.executable)
//		else:
//			def inspect_this(): pass
//			path = inspect.getabsfile(inspect_this)
//		self._executable = os.path.realpath(path)
//		return self._executable
//
//	@property
//	def jython_home(self):
//		if hasattr(self, "_jython_home"):
//			return self._jython_home
//		if "JYTHON_HOME" in os.environ:
//			self._jython_home = os.environ["JYTHON_HOME"]
//		else:
//			self._jython_home = os.path.dirname(os.path.dirname(self.executable))
//		if self.uname == "cygwin":
//			self._jython_home = subprocess.check_output(["cygpath", "--windows", self._jython_home]).strip()
//		return self._jython_home
//
//	@property
//	def jython_opts():
//		return os.environ.get("JYTHON_OPTS", "")
//
//	@property
//	def classpath_delimiter(self):
//		return ";" if (is_windows or self.uname == "cygwin") else ":"
//
//	@property
//	def jython_jars(self):
//		if hasattr(self, "_jython_jars"):
//			return self._jython_jars
//		if os.path.exists(os.path.join(self.jython_home, "jython-dev.jar")):
//			jars = [os.path.join(self.jython_home, "jython-dev.jar")]
//			if self.args.boot:
//				// Wildcard expansion does not work for bootclasspath
//				for jar in glob.glob(os.path.join(self.jython_home, "javalib", "*.jar")):
//					jars.append(jar)
//			else:
//				jars.append(os.path.join(self.jython_home, "javalib", "*"))
//		elif not os.path.exists(os.path.join(self.jython_home, "jython.jar")):
//			bad_option("""{jython_home} contains neither jython-dev.jar nor jython.jar.
//Try running this script from the 'bin' directory of an installed Jython or
//setting {envvar_specifier}JYTHON_HOME.""".format(
//					jython_home=self.jython_home,
//					envvar_specifier="%" if self.uname == "windows" else "$"))
//		else:
//			jars = [os.path.join(self.jython_home, "jython.jar")]
//		self._jython_jars = jars
//		return self._jython_jars
//
//	@property
//	def java_classpath(self):
//		if hasattr(self.args, "classpath"):
//			return self.args.classpath
//		else:
//			return os.environ.get("CLASSPATH", ".")
//
//	@property
//	def java_mem(self):
//		if hasattr(self.args, "mem"):
//			return self.args.mem
//		else:
//			return os.environ.get("JAVA_MEM", "-Xmx512m")
//
//	@property
//	def java_stack(self):
//		if hasattr(self.args, "stack"):
//			return self.args.stack
//		else:
//			return os.environ.get("JAVA_STACK", "-Xss1024k")
//
//	@property
//	def java_opts(self):
//		return [self.java_mem, self.java_stack]
//
//	@property
//	def java_profile_agent(self):
//		return os.path.join(self.jython_home, "javalib", "profile.jar")
//
//	def set_encoding(self):
//		if "JAVA_ENCODING" not in os.environ and self.uname == "darwin" and "file.encoding" not in self.args.properties:
//			self.args.properties["file.encoding"] = "UTF-8"
//
//	def convert(self, arg):
//		if sys.stdout.encoding:
//			return arg.encode(sys.stdout.encoding)
//		else:
//			return arg
//
//	def make_classpath(self, jars):
//		return self.classpath_delimiter.join(jars)
//
//	def convert_path(self, arg):
//		if self.uname == "cygwin":
//			if not arg.startswith("/cygdrive/"):
//				new_path = self.convert(arg).replace("/", "\\")
//			else:
//				new_path = subprocess.check_output(["cygpath", "-pw", self.convert(arg)]).strip()
//			return new_path
//		else:
//			return self.convert(arg)
//
//	@property
//	def command(self):
//		self.set_encoding()
//		args = [self.java_command]
//		args.extend(self.java_opts)
//		args.extend(self.args.java)
//
//		classpath = self.java_classpath
//		jython_jars = self.jython_jars
//		if self.args.boot:
//			args.append("-Xbootclasspath/a:%s" % self.convert_path(self.make_classpath(jython_jars)))
//		else:
//			classpath = self.make_classpath(jython_jars) + self.classpath_delimiter + classpath
//		args.extend(["-classpath", self.convert_path(classpath)])
//
//		if "python.home" not in self.args.properties:
//			args.append("-Dpython.home=%s" % self.convert_path(self.jython_home))
//		if "python.executable" not in self.args.properties:
//			args.append("-Dpython.executable=%s" % self.convert_path(self.executable))
//		if "python.launcher.uname" not in self.args.properties:
//			args.append("-Dpython.launcher.uname=%s" % self.uname)
//		// Determines whether running on a tty for the benefit of
//		// running on Cygwin. This step is needed because the Mintty
//		// terminal emulator doesn't behave like a standard Microsoft
//		// Windows tty, and so JNR Posix doesn't detect it properly.
//		if "python.launcher.tty" not in self.args.properties:
//			args.append("-Dpython.launcher.tty=%s" % str(os.isatty(sys.stdin.fileno())).lower())
//		if self.uname == "cygwin" and "python.console" not in self.args.properties:
//			args.append("-Dpython.console=org.python.core.PlainConsole")
//		if self.args.profile:
//			args.append("-XX:-UseSplitVerifier")
//			args.append("-javaagent:%s" % self.convert_path(self.java_profile_agent))
//		for k, v in self.args.properties.iteritems():
//			args.append("-D%s=%s" % (self.convert(k), self.convert(v)))
//		args.append("org.python.util.jython")
//		if self.args.help:
//			args.append("--help")
//		args.extend(self.jython_args)
//		return args

static char* usage_0 = "\
usage: jython [option] ... [-c cmd | -m mod | file | -] [arg] ...\n\
Try `jython -h' for more information.\n\
";
void bad_option(char* msg) {
	fputs(msg, stderr);
	fputs(usage_0, stderr);
	exit(2);
}

static char* usage_1 = "\
Jython launcher-specific options:\n\
-Dname=value : pass name=value property to Java VM (e.g. -Dpython.path=/a/b/c)\n\
-Jarg    : pass argument through to Java VM (e.g. -J-Xmx512m)\n\
--boot   : speeds up launch performance by putting Jython jars on the boot classpath\n\
--help   : this help message\n\
--jdb    : run under JDB java debugger\n\
--print  : print the Java command with args for launching Jython instead of executing it\n\
";

static char* usage_2 = "\
--profile: run with the Java Interactive Profiler (http://jiprof.sf.net)\n\
--       : pass remaining arguments through to Jython\n\
Jython launcher environment variables:\n\
JAVA_MEM   : Java memory (sets via -Xmx)\n\
JAVA_OPTS  : options to pass directly to Java\n\
JAVA_STACK : Java stack size (sets via -Xss)\n\
JAVA_HOME  : Java installation directory\n\
JYTHON_HOME: Jython installation directory\n\
JYTHON_OPTS: default command line arguments\n\
";

void print_help()
{
	fputs(usage_1, stderr);
	fputs(usage_2, stderr);
}

//def support_java_opts(args):
//	it = iter(args)
//	while it:
//		arg = next(it)
//		if arg.startswith("-D"):
//			yield arg
//		elif arg in ("-classpath", "-cp"):
//			yield "-J" + arg
//			try:
//				yield next(it)
//			except StopIteration:
//				bad_option("Argument expected for -classpath option in JAVA_OPTS")
//		else:
//			yield "-J" + arg
//
//
//// copied from subprocess module in Jython; see
//// http://bugs.python.org/issue1724822 where it is discussed to include
//// in Python 3.x for shlex:
//def cmdline2list(cmdline):
//	"""Build an argv list from a Microsoft shell style cmdline str
//
//	The reverse of list2cmdline that follows the same MS C runtime
//	rules.
//	"""
//	whitespace = ' \t'
//	// count of preceding '\'
//	bs_count = 0
//	in_quotes = False
//	arg = []
//	argv = []
//
//	for ch in cmdline:
//		if ch in whitespace and not in_quotes:
//			if arg:
//				// finalize arg and reset
//				argv.append(''.join(arg))
//				arg = []
//			bs_count = 0
//		elif ch == '\\':
//			arg.append(ch)
//			bs_count += 1
//		elif ch == '"':
//			if not bs_count % 2:
//				// Even number of '\' followed by a '"'. Place one
//				// '\' for every pair and treat '"' as a delimiter
//				if bs_count:
//					del arg[-(bs_count / 2):]
//				in_quotes = not in_quotes
//			else:
//				// Odd number of '\' followed by a '"'. Place one '\'
//				// for every pair and treat '"' as an escape sequence
//				// by the remaining '\'
//				del arg[-(bs_count / 2 + 1):]
//				arg.append(ch)
//			bs_count = 0
//		else:
//			// regular char
//			arg.append(ch)
//			bs_count = 0
//
//	// A single trailing '"' delimiter yields an empty arg
//	if arg or in_quotes:
//		argv.append(''.join(arg))
//
//	return argv
//
//
//def decode_args(sys_args):
//	args = [sys_args[0]]
//
//	def get_env_opts(envvar):
//		opts = os.environ.get(envvar, "")
//		if is_windows:
//			return cmdline2list(opts)
//		else:
//			return shlex.split(opts)
//
//	java_opts = get_env_opts("JAVA_OPTS")
//	jython_opts = get_env_opts("JYTHON_OPTS")
//
//	args.extend(support_java_opts(java_opts))
//	args.extend(sys_args[1:])
//
//	if sys.stdout.encoding:
//		if sys.stdout.encoding.lower() == "cp65001":
//			sys.exit("""Jython does not support code page 65001 (CP_UTF8).
//Please try another code page by setting it with the chcp command.""")
//		args = [arg.decode(sys.stdout.encoding) for arg in args]
//		jython_opts = [arg.decode(sys.stdout.encoding) for arg in jython_opts]
//
//	return args, jython_opts

#define jylibdir "/javalib/*"
void prepareClasspath(JySetup* setup, char* jythonHome,
		char* jythonJar, jboolean boot, jboolean freeOld)
{
	int jhl = strlen(jythonHome);
	int jjr = strlen(jythonJar);
	int jll = sizeof(jylibdir)-1;
	int arl = strlen(setup->cp);
	char* cpNew = malloc((jjr+jhl+jll+(!boot ? arl+3 : 2))*sizeof(char));
	//+3 for 2*separator + null-termination
	char* cpOff = cpNew;
	strcpy(cpOff, jythonJar);
	cpOff += jjr;
	cpOff[0] = PATH_SEPARATOR;
	cpOff += 1;
	strcpy(cpOff, jythonHome);
	cpOff += jhl;
	strcpy(cpOff, jylibdir);
	cpOff += jll;
	if (!boot) {
		cpOff[0] = PATH_SEPARATOR;
		cpOff += 1;
		strcpy(cpOff, setup->cp);
		cpOff += arl;
	}
	cpOff[0] = 0;
	if (freeOld) free(setup->cp);
	setup->cp = cpNew;
}

// /usr/local/java/jdk1.8.0_25/lib/tools.jar:/usr/local/java/jdk1.8.0_25/lib/sa-jdi.jar
#define toolsjar "/lib/tools.jar"
#define sajdijar "/lib/sa-jdi.jar"
#define jreclasses "/classes"
void prepareJdbClasspath(char* jrePath, char* dest) {
	int jrl = strlen(jrePath);
	char* jre = strrchr(jrePath, '/');
	char* cpOff = dest;
	strncpy(cpOff, jrePath, jre-jrePath);
	cpOff += (jre-jrePath);
	strcpy(cpOff, toolsjar);
	cpOff += sizeof(toolsjar)-1;
	cpOff[0] = PATH_SEPARATOR;
	cpOff += 1;
	strncpy(cpOff, jrePath, jre-jrePath);
	cpOff += (jre-jrePath);
	strcpy(cpOff, sajdijar);
	cpOff += sizeof(sajdijar)-1;
	cpOff[0] = PATH_SEPARATOR;
	cpOff += 1;
	strcpy(cpOff, jrePath);
	cpOff += jrl;
	strcpy(cpOff, jreclasses);
}

int prepareJdbClasspathLen(char* jrePath) {
	char* jre = strrchr(jrePath, '/');
	return 2*(jre-jrePath) + sizeof(toolsjar) + sizeof(sajdijar)
			+ strlen(jrePath) + sizeof(jreclasses)-2;
}

int Jython_Main(int argc, char ** argv,         /* main argc, argc */
        int jargc, const char** jargv,          /* java args */
        int appclassc, const char** appclassv,  /* app classpath */
        const char* pname,                      /* program name */
        const char* lname,                      /* launcher name */
        jboolean javaargs,                      /* JAVA_ARGS */
        jboolean cpwildcard,                    /* classpath wildcard */
        jboolean javaw,                         /* windows-only javaw */
        jint     ergo_class                     /* ergnomics policy */
)
{

//	puts("Jython_Main\n");
//	printf("Command line args:\n");
//	int i;
//	for (i = 0; i < argc ; i++) {
//		printf("argv[%d] = %s\n", i, argv[i]);
//	}
//	puts("\n");
	JySetup* setup = parse_launcher_args(argc, argv);
	//printSetup(setup);
//	if (setup->print_requested) {
//		puts("Error: --print is currently not supported by LiJy-launch.");
//		exit(1);
//	}
//	sys_args, jython_opts = decode_args(sys_args)
//	args, jython_args = parse_launcher_args(sys_args)
//	jython_command = JythonCommand(args, jython_opts + jython_args)
//	command = jython_command.command

//	printf("Command line jargs:\n");
//	for (i = 0; i < setup->javaCount ; i++) {
//		printf("jargv[%d] = %s\n", i, setup->java[i]);
//	}
//	puts("-----");
//	printf("Command line jyargs:\n");
//	for (i = 0; i < setup->jythonCount ; i++) {
//		printf("jargv[%d] = %s\n", i, setup->jython[i]);
//	}
//	puts("=====");
	//int result = JLI_Launch(argc-setup->argOff, argv+setup->argOff,
	int result = JLI_Launch(setup->jythonCount, setup->jython,
			//jargc, jargv,         /* java args */
			setup->javaCount, setup->java,
			appclassc, appclassv, /* app classpath */
			pname,                /* program name */
			lname,                /* launcher name */
			javaargs,             /* JAVA_ARGS */
			cpwildcard,           /* classpath wildcard */
			javaw,                /* windows-only javaw */
			ergo_class,           /* ergnomics policy */
			setup
		);
	freeSetup(setup);
	return result;
}
//	if args.profile and not args.help:
//		try:
//			os.unlink("profile.txt")
//		except OSError:
//			pass
//	if args.print_requested and not args.help:
//		if jython_command.uname == "windows":
//			print subprocess.list2cmdline(jython_command.command)
//		else:
//			print " ".join(pipes.quote(arg) for arg in jython_command.command)
//	else:
//		if not (is_windows or not hasattr(os, "execvp") or args.help or jython_command.uname == "cygwin"):
//			// Replace this process with the java process.
//			#
//			// NB such replacements actually do not work under Windows,
//			// but if tried, they also fail very badly by hanging.
//			// So don't even try!
//			os.execvp(command[0], command[1:])
//		else:



//			result = 1
//			try:
//				print "Would call jython now:"
//				print command
////				result = subprocess.call(command)
//				if args.help:
//					print_help()
//			except KeyboardInterrupt:
//				pass
//			sys.exit(result)

