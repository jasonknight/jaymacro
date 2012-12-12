/***************************************************************************** 
 *
 * xmacroplay - a utility for playing X mouse and key events.
 * Portions Copyright (C) 2000 Gabor Keresztfalvi <keresztg@mail.com>
 *
 * The recorded events are read from the standard input.
 *
 * This program is heavily based on
 * xremote (http://infa.abo.fi/~chakie/xremote/) which is:
 * Copyright (C) 2000 Jan Ekholm <chakie@infa.abo.fi>
 *	
 * This program is free software; you can redistribute it and/or modify it  
 * under the terms of the GNU General Public License as published by the  
 * Free Software Foundation; either version 2 of the License, or (at your 
 * option) any later version.
 *	
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
 * for more details.
 *	
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 ****************************************************************************/

/***************************************************************************** 
 * Do we have config.h?
 ****************************************************************************/
#ifdef HAVE_CONFIG
#include "config.h"
#endif

/***************************************************************************** 
 * Includes
 ****************************************************************************/
#include <stdio.h>		
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <ctype.h>
#include <X11/Xlibint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysymdef.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <iostream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <ext/hash_map>
#include <sstream>
#include <unordered_map>
#include <algorithm> 
#include <functional> 
#include <locale>
#include <map>
#include <boost/regex.hpp>
#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
using namespace __gnu_cxx;

#include "chartbl.h"
/***************************************************************************** 
 * What iostream do we have?
 ****************************************************************************/

#define PROG "xmacroplay"
#define _VSTRING 1
#define _VNUMBER 2
#define _VDOUBLE 3
#define _VINT    4
#define _VFILE   3

struct file_object {
  int cpos;
  const char * name;
  int length;
};
class Variable {
  public:
    Variable();
    ~Variable();
    void Set(std::string str) {
      p_data_as_string = str;
      p_type = _VSTRING;
    }
    std::string ToString() {
      return p_data_as_string;
    }
  private:
    int p_type;
    std::string p_data_as_string;
};
/***************************************************************************** 
 * The delay in milliseconds when sending events to the remote display
 ****************************************************************************/
const int DefaultDelay = 10;
int MouseDelay = DefaultDelay;
 int KeyPressDelay = DefaultDelay;
/***************************************************************************** 
 * The multiplier used fot scaling coordinates before sending them to the
 * remote display. By default we don't scale at all
 ****************************************************************************/
const float DefaultScale = 1.0;

/***************************************************************************** 
 * Globals...
 ****************************************************************************/

int _SourceLines = 150;
int   Delay = DefaultDelay;
float Scale = DefaultScale;
char * Remote;
int Entry = NULL;
std::string * Source = new std::string[_SourceLines];
int SourceNumLines = 0;
std::map<std::string,int> Labels;
int Index = 0;
Display * GlobalDisplay;
int GlobalScreen;
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Registers is where we hold all of our variables, you can put anything you 
 * like in there, any variable name, however, some registers are reserved
 *
 * All variables are saved as strings, and only converted to ints if necessary
 * 
 * CS is the current CallStackPtr, it is an int.
 *
 * SCS is where break sends you, it is the position in the main loop when you 
 * first called goto
 *
 *  * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
std::map<std::string,std::string> Registers;
std::map<std::string,Variable *> Variables; 
std::map<std::string,file_object *> OpenFiles;

int CallStackPtr = 0;
int _StackDepth = 60480;
int CallStack[6048];


// we put the function sigs here so that they are accessible
static inline void executeLine(std::string &sline);
static inline void executeIf(std::string &sline);
static inline bool expressionResult(std::string &ltoken, 
                                    std::string &comp, 
                                    std::string & rtoken);
static inline void saveRegexResult(boost::cmatch &what);


namespace Parser {
  namespace qi = boost::spirit::qi;
  namespace ascii = boost::spirit::ascii;
  namespace phoenix = boost::phoenix;
  using qi::double_;
  using qi::_1;
  using qi::phrase_parse;
  using ascii::space;
  using phoenix::ref;

  template <typename Iterator>
  bool result(Iterator first, Iterator last, double &n) {

    bool r = qi::phrase_parse (
          first,
          last,
          // Parser begins here
          (
             double_[ref(n) = _1] >> (*("+" >> double_[ref(n) += _1]) || *("-" >> double_[ref(n) -= _1]))
          ),
          // Parser ends here
          space
        );
    if (first != last)
      return false;
    return true;
  }

}

/****************************************************************************/
/*! Prints the usage, i.e. how the program is used. Exits the application with
    the passed exit-code.

	\arg const int ExitCode - the exitcode to use for exiting.
*/
/****************************************************************************/
void usage (const int exitCode) {

  // print the usage
  std::cerr << PROG << " " << VERSION << std::endl;
  std::cerr << "Usage: " << PROG << " [options] remote_display" << std::endl;
  std::cerr << "Options: " << std::endl;
  std::cerr << "  -d  DELAY   delay in milliseconds for events sent to remote display." << std::endl
	   << "              Default: 10ms."
	   << std::endl
	   << "  -s  FACTOR  scalefactor for coordinates. Default: 1.0." << std::endl
	   << "  -v          show version. " << std::endl
	   << "  -h          this help. " << std::endl << std::endl;

  // we're done
  exit ( EXIT_SUCCESS );
}


/****************************************************************************/
/*! Prints the version of the application and exits.
*/
/****************************************************************************/
void version () {

  // print the version
  std::cerr << PROG << " " << VERSION << std::endl;
 
  // we're done
  exit ( EXIT_SUCCESS );
}


/****************************************************************************/
/*! Parses the commandline and stores all data in globals (shudder). Exits
    the application with a failed exitcode if a parameter is illegal.

	\arg int argc - number of commandline arguments.
	\arg char * argv[] - vector of the commandline argument strings.
*/
/****************************************************************************/
void parseCommandLine (int argc, char * argv[]) {

  int Index = 1;
  
  // check the number of arguments
  if ( argc < 2 ) {
	// oops, too few arguments, go away
	usage ( EXIT_FAILURE );
  }

  // loop through all arguments except the last, which is assumed to be the
  // name of the display
  while ( Index < argc ) {
	
	// is this '-v'?
	if ( strcmp (argv[Index], "-v" ) == 0 ) {
	  // yep, show version and exit
	  version ();
	}

	// is this '-h'?
	if ( strcmp (argv[Index], "-h" ) == 0 ) {
	  // yep, show usage and exit
	  usage ( EXIT_SUCCESS );
	}

	// is this '-d'?
	else if ( strcmp (argv[Index], "-d" ) == 0 && Index + 1 < argc ) {
	  // yep, and there seems to be a parameter too, interpret it as a
	  // number
	  if ( sscanf ( argv[Index + 1], "%d", &Delay ) != 1 ) {
		// oops, not a valid intereger
		std::cerr << "Invalid parameter for '-d'." << std::endl;
		usage ( EXIT_FAILURE );
	  }
	  
	  Index++;
	}

	// is this '-s'?
	else if ( strcmp (argv[Index], "-s" ) == 0 && Index + 1 < argc ) {
	  // yep, and there seems to be a parameter too, interpret it as a
	  // floating point number
	  if ( sscanf ( argv[Index + 1], "%f", &Scale ) != 1 ) {
		// oops, not a valid intereger
		std::cerr << "Invalid parameter for '-s'." << std::endl;
		usage ( EXIT_FAILURE );
	  }
	  
	  Index++;
	}

	// is this the last parameter?
	else if ( Index == argc - 2 ) {
	  // yep, we assume it's the display, store it
	  Remote = argv [ Index ];
	}

	else {
	  // we got this far, the parameter is no good...
	  //std::cerr << "Invalid parameter '" << argv[Index] << "'." << std::endl;
	  //usage ( EXIT_FAILURE );
	}

	// next value
	Index++;
  }
}

/****************************************************************************/
/*! Connects to the desired display. Returns the \c Display or \c 0 if
    no display could be obtained.

	\arg const char * DisplayName - name of the remote display.
*/
/****************************************************************************/
Display * remoteDisplay (const char * DisplayName) {

  int Event, Error;
  int Major, Minor;  

  // open the display
  Display * D = XOpenDisplay ( DisplayName );

  // did we get it?
  if ( ! D ) {
	// nope, so show error and abort
	std::cerr << PROG << ": could not open display \"" << XDisplayName ( DisplayName )
		 << "\", aborting." << std::endl;
	exit ( EXIT_FAILURE );
  }

  // does the remote display have the Xtest-extension?
  if ( ! XTestQueryExtension (D, &Event, &Error, &Major, &Minor ) ) {
	// nope, extension not supported
	std::cerr << PROG << ": XTest extension not supported on server \""
		 << DisplayString(D) << "\"" << std::endl;

	// close the display and go away
	XCloseDisplay ( D );
	exit ( EXIT_FAILURE );
  }

  // print some information
  std::cerr << "XTest for server \"" << DisplayString(D) << "\" is version "
	   << Major << "." << Minor << "." << std::endl << std::endl;;

  // execute requests even if server is grabbed 
  XTestGrabControl ( D, True ); 

  // sync the server
  XSync ( D,True ); 

  // return the display
  return D;
}

/****************************************************************************/
/*! Scales the passed coordinate with the given saling factor. the factor is
    either given as a commandline argument or it is 1.0.
*/
/****************************************************************************/
int scale (const int Coordinate) {

  // perform the scaling, all in one ugly line
  return (int)( (float)Coordinate * Scale );
}

/****************************************************************************/
/*! Sends a \a character to the remote display \a RemoteDpy. The character is
    converted to a \c KeySym based on a character table and then reconverted to
	a \c KeyCode on the remote display. Seems to work quite ok, apart from
	something weird with the Alt key.

    \arg Display * RemoteDpy - used display.
	\arg char c - character to send.
*/
/****************************************************************************/
void sendChar(Display *RemoteDpy, char c)
{
	KeySym ks, sks, *kss, ksl, ksu;
	KeyCode kc, skc;
	int syms;
#ifdef DEBUG
	int i;
#endif

	sks=XK_Shift_L;

	ks=XStringToKeysym(chartbl[0][(unsigned char)c]);
	if ( ( kc = XKeysymToKeycode ( RemoteDpy, ks ) ) == 0 )
	{
  		std::cerr << "No keycode on remote display found for char: " << c << std::endl;
	  	return;
	}
	if ( ( skc = XKeysymToKeycode ( RemoteDpy, sks ) ) == 0 )
	{
  		std::cerr << "No keycode on remote display found for XK_Shift_L!" << std::endl;
	  	return;
	}

	kss=XGetKeyboardMapping(RemoteDpy, kc, 1, &syms);
	if (!kss)
	{
  		std::cerr << "XGetKeyboardMapping failed on the remote display (keycode: " << kc << ")" << std::endl;
	  	return;
	}
	for (; syms && (!kss[syms-1]); syms--);
	if (!syms)
	{
  		std::cerr << "XGetKeyboardMapping failed on the remote display (no syms) (keycode: " << kc << ")" << std::endl;
		XFree(kss);
	  	return;
	}
	XConvertCase(ks,&ksl,&ksu);
#ifdef DEBUG
	std::cout << "kss: ";
	for (i=0; i<syms; i++) std::cout << kss[i] << " ";
	std::cout << "(" << ks << " l: " << ksl << "  h: " << ksu << ")" << std::endl;
#endif
	if (ks==kss[0] && (ks==ksl && ks==ksu)) sks=NoSymbol;
	if (ks==ksl && ks!=ksu) sks=NoSymbol;
	if (sks!=NoSymbol) XTestFakeKeyEvent ( RemoteDpy, skc, True, Delay );
	XTestFakeKeyEvent ( RemoteDpy, kc, True, Delay );
	XFlush ( RemoteDpy );
	XTestFakeKeyEvent ( RemoteDpy, kc, False, Delay );
	if (sks!=NoSymbol) XTestFakeKeyEvent ( RemoteDpy, skc, False, Delay );
	XFlush ( RemoteDpy );
	XFree(kss);
}
/*
 Trim whitespace so scripts can be well formatted.
*/
char* trimWhitespace(char *str)
{
  char *end;

  // Trim leading space
  while(isspace(*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace(*end)) end--;

  // Write new null terminator
  *(end+1) = 0;
  return str;
}
/*****************************************************************************
 * Convert std::string to char *
 * **************************************************************************/

char * stringToCharz(std::string str) {
  char * new_str = new char[str.size() + 1];
  std::copy(str.begin(), str.end(), new_str);
  new_str[str.size()] = '\0';
  return new_str;
}
double stringToDouble(std::string str) {
  std::stringstream s;
  s.str(str);
  double d;
  s >> d;
  return d;
}
int stringToInt(std::string str) {
  std::stringstream s;
  s.str(str);
  int d;
  s >> d;
  return d;
}
std::string doubleToString(double d) {
  std::stringstream s;
  s.str("");
  s << d;
  return s.str();
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Xlib Helper Functions
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
Window recursiveWindowSearch(std::string &keywords,Window window,int recurse,int level) {
  Window root_win, parent_win;
  unsigned int num_children;
  Window *child_list;
  XClassHint classhint;
  XTextProperty name;
  int i;
  if (!XQueryTree(GlobalDisplay, window, &root_win, &parent_win, &child_list, &num_children)) {
    std::cout << "Recursive returns null to query tree" << std::endl;
    return NULL;
  }
  for (i = (int)num_children - 1; i >= 0; i--) {
    if (XGetWMName(GlobalDisplay,child_list[i],&name)) {
      std::cout << "Recursive Search Name: " << name.value << std::endl;
      std::string s_name = (char *)name.value;
      if (s_name.find(keywords) != std::string::npos) {
        std::cout << "Returning found window" << std::endl;
        return child_list[i];
      } 
    }
    Window t;
    if (t = recursiveWindowSearch(keywords,child_list[i],1,++level)) {
      return t;
    }
  }
  return NULL;
}
Window GetWindowByName(std::string &keywords) {
  Window window, rootwindow;
  rootwindow = RootWindow(GlobalDisplay,DefaultScreen(GlobalDisplay));
  Atom atom = XInternAtom(GlobalDisplay, "_NET_CLIENT_LIST", True);
  XWindowAttributes attr;
  Atom atom_event;
  XEvent xev;
  Atom actualType;
  int format;
  unsigned long numItems, bytesAfter;
  unsigned char *data = 0;
  int status = XGetWindowProperty(GlobalDisplay,
                                  rootwindow,
                                  atom,
                                  0L,
                                  (~0L),
                                  false,
                                  AnyPropertyType,
                                  &actualType,
                                  &format,
                                  &numItems,
                                  &bytesAfter,
                                  &data);
  if (status >= Success && numItems) {
    int * array = (int *)data;
    for (int k = 0; k < numItems; k++) {
      window = (Window)array[k];
      XTextProperty name;
//       std::cout << "Fetching Name" << std::endl;
      if (window != NULL) {
        XGetWMName(GlobalDisplay,window,&name);
        std::string s_name = (char *)name.value;
        std::cout << "Name: " << name.value << std::endl;
        if (s_name.find(keywords) != std::string::npos) {
          std::cout << "Returning found window" << std::endl;
          return window;
        } else {
          std::cout << "Recursive Search Started" << std::endl;
          Window t = recursiveWindowSearch(keywords,window,1,1);
          if (t!= NULL) {
            return t;
          }
        }
      } else {
        std::cout << "Window is null" << std::endl;
      }
    } // end for
  }
  return window;
}
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Trim whitespace with an std::string
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
// trim from start
static inline std::string &ltrim(std::string &s) {
//   std::cout << "ltrim recv: " << s << std::endl;
  if (s.empty())
    return s;
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
  return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
//    std::cout << "rtrim recv: " << s << std::endl;
  if (s.empty())
    return s;
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
  return s;
}
// trim from both ends
static inline std::string &trim(std::string &s) {
//   std::cout << "trying to trim : '" << s << "'" << std::endl;
  if (s.empty()) {
//     std::cout << "s is empty: '" << s << "'" << std::endl;
    return s;
  }
  return ltrim(rtrim(s));
}
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *  Parse a string for special characters.
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static inline std::string &parseSpecialChars(std::string &s) {
  size_t found;
  found = s.find("\\n");
  if (found != std::string::npos) {
    while ((found = s.find("\\n")) != std::string::npos) {
      s.replace(found,2,"\n");
    }
  }
  found = s.find("\\t");
  if (found != std::string::npos) {
    while ((found = s.find("\\t")) != std::string::npos) {
      s.replace(found,2,"\t");
    }
  }
  found = s.find("\\r");
  if (found != std::string::npos) {
    while ((found = s.find("\\r")) != std::string::npos) {
      s.replace(found,2,"\r");
    }
  }
  found = s.find("0x");
  if (found != std::string::npos) {
    while ((found = s.find("0x")) != std::string::npos) {
      size_t end = s.find(" ",found);
      std::string sub = s.substr(found,4);
      std::stringstream ss(sub);
      uint32_t v;
      ss >> std::setbase(0) >> v;
      char str[1];
      sprintf(str,"%c",v);
      s.replace(found,sub.size(),str,1);
    }
  }
  found = s.find("${");
  if (found != std::string::npos) {
    while ((found = s.find("${")) != std::string::npos) {
      size_t end = s.find("}",found);
      std::string sub = s.substr(found + 2,end - found - 2);
      s.replace(found,sub.size() + 3, Registers[sub].c_str(),Registers[sub].size());
    }
  }
  double n;
  if (Parser::result(s.begin(),s.end(),n)) {
    std::stringstream ss;
    ss.str("");
    ss << n;
    s = ss.str();
  }
  return s;
}
static inline void saveRegexResult(boost::smatch &what) {
    int i;
    std::stringstream sind;
    for(i = 0; i < what.size(); ++i) {
      sind << i;
      Registers[sind.str()] = what[i];
      sind.str("");
    }
      
}
static inline bool isPostIf(std::string &str) {
  boost::regex expr("(.*) if (.*)");
  boost::smatch what;
  if (boost::regex_match(str,what,expr)) {
    
    boost::regex expr2("(.*) (is|not|like) (.*)");
    boost::smatch what2;
    
    std::stringstream line;
    std::string nstr = what[2];
    

    boost::regex_match(nstr,what2,expr2);

    line.str(nstr);
    std::string ltoken,comp,rtoken;
    ltoken = what2[1];
    comp = what2[2];
    rtoken = what2[3];
    ltoken = parseSpecialChars(ltoken);
    rtoken = parseSpecialChars(rtoken);
    bool istrue = expressionResult(ltoken,comp,rtoken);
    if (istrue) {
      std::string x = what[1];
//       std::cout << "is true, executing line: " << x << std::endl;
      executeLine(x);
    }
    return true;
  }
  return false;
}
static inline bool expressionResult(std::string &ltoken, 
                                    std::string &comp, 
                                    std::string & rtoken)
{
//      std::cout << "(" << ltoken << " " << comp << " " << rtoken << ")" << std::endl;
    if (comp == "is" && ltoken == rtoken)
      return true;
    if (comp == "not" && ltoken != rtoken)
      return true;
    if (comp == "like") {
      // we have to use a regular expression, which should be rtoken
      boost::regex expr(rtoken);
      boost::smatch what;
      if (boost::regex_match(ltoken,what,expr)) {
        saveRegexResult(what);
        return true;
      }
    }
    return false;
}

static inline void executeIf(std::string &sline) {
    std::stringstream line;
    line.str(sline);
    std::string ltoken,comp,rtoken;
    bool istrue = false;
    line >> ltoken;
    line >> ltoken >> comp >> rtoken;
    ltoken = parseSpecialChars(ltoken);
    rtoken = parseSpecialChars(rtoken);
    //std::cout << "Tokens: " << ltoken << " " << rtoken << std::endl;
    istrue = expressionResult(ltoken,comp,rtoken);
    if (istrue) {
      Index++;
      while(Source[Index].find("endif") == std::string::npos) {
        executeLine(Source[Index]);
        Index++;
      }
      return;
    } else {
      while(Source[Index].find("endif") == std::string::npos) {
        Index++;
      }
    }

}

static inline void executeLine(std::string &sline) {
    std::stringstream myfile;
    char ev[200], str[1024], reg[1];
    int x, y,entry,ret;
    unsigned int b;
    KeySym ks;
    KeyCode kc;

    myfile << sline;
	  myfile >> ev;
//      std::cout << "\t\t\t\t\t\tev: " << ev << std::endl;
	  char * nev = trimWhitespace(ev);
	  strcpy(ev,nev);
	  if (ev[0]=='#')
	  {
	    std::cout << "Comment: " << ev << std::endl;
      return;
	  }
	  else if (!strcasecmp("End",ev))
	  {
	    exit(0);
	  }
    else if (!strcasecmp("EndL",ev))
	  {
      std::cout << std::endl;
	  }

	  else if (!strcasecmp("Set",ev))
	  {
      std::string reg;
      std::string value;
      myfile >> reg;
      if (reg.find("++") != std::string::npos) {
          reg = reg.replace(reg.find("++"),2,"");
          double d = stringToDouble(Registers[reg]);
          d++;
          Registers[reg] = doubleToString( d );
      } else if (reg.find("--") != std::string::npos) {
          reg = reg.replace(reg.find("--"),2,"");
          double d = stringToDouble(Registers[reg]);
          d--;
          Registers[reg] = doubleToString( d );
      } else {
          myfile.getline(str,1024);
          value = str;
	        Registers[reg] = parseSpecialChars(trim(value));
      }
	  }
    else if (!strcasecmp("FileOpen",ev))
	  {
      std::string vname;
      std::string fpath;
      myfile >> vname;
      myfile.getline(str,1024);
      fpath = str;
      file_object ff;
      file_object * fob;
      fob = &ff;
      fpath = trim(parseSpecialChars(fpath));
      fob->name = stringToCharz(fpath);
      std::ifstream f(fpath.c_str());
      f.seekg(0, std::ios::end);
      fob->length = f.tellg();
      f.seekg(0,std::ios::beg);
      fob->cpos = f.tellg();
      f.close();
      OpenFiles[vname] = fob; 
	  }
    else if (!strcasecmp("FileReadAll",ev))
	  {
      std::string desc;
      std::string target;
      myfile >> desc;
      myfile >> target;
      std::ifstream f(OpenFiles[desc]->name);
      std::string str((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      Registers[target] = str;
      f.close();
	  }
    else if (!strcasecmp("FileLength",ev))
	  {
      std::string desc;
      std::string target;
      myfile >> desc;
      myfile >> target;
	    int length;
      std::stringstream s;
      s << OpenFiles[desc]->length;
      Registers[target] = s.str();
	  }
    else if (!strcasecmp("In",ev))
	  {
      std::string reg;
      std::string value;
      myfile >> reg;
      myfile.getline(str,1024);
      std::cout << trimWhitespace(str) << " ";
      std::cin >> value;
	    Registers[reg] = trim(value);
	  }
    else if (!strcasecmp("If",ev))
	  {
      executeIf(Source[Index]);
	  }

	  else if (!strcasecmp("Preg",ev))
	  {
	    myfile >> str;
	    std::string s = str;
      std::cout << Registers[s] << std::endl;
	  }
	  else if (!strcasecmp("Delay",ev))
	  {
	    myfile >> b;
	    std::cout << "Delay: " << b << std::endl;
	    sleep ( b );
	  }
	  else if (!strcasecmp("SetMouseDelay",ev))
    {
      myfile >> b;
      std::cout << "Delay: " << b << std::endl;
      MouseDelay = b;
    }
    else if (!strcasecmp("SetKeyPressDelay",ev))
    {
      myfile >> b;
      std::cout << "Delay: " << b << std::endl;
      KeyPressDelay = b;
    }
	  else if (!strcasecmp("USleep",ev))
	  {
	    myfile >> b;
	    std::cout << "USleep: " << b << std::endl;
	    usleep ( b );
	  }
	  else if (!strcasecmp("Print",ev))
	  {
      std::string text;
	    myfile.getline(str,1024);
      text = str;
	    std::cout << parseSpecialChars(trim(text));
	  }
	  else if (!strcasecmp("Restart",ev))
	  {
	    myfile.seekg(0);
	  }
	  else if (!strcasecmp("Return",ev))
	  {
      //std::cout << "Returning" << std::endl;
      CallStackPtr--;
      if (CallStackPtr < 0) {
        CallStackPtr = 0;
      }
	    Index = CallStack[CallStackPtr];
      executeLine(Source[Index]);
	  }
    else if (!strcasecmp("Break",ev))
	  {
      std::string scs = "SCS";
      Index = atoi(Registers[scs].c_str()) ; 
		  CallStackPtr = 0;  
      executeLine(Source[Index]);
    }

	  else if (!strcasecmp("Goto",ev))
	  {
      myfile >> str;
	    CallStack[CallStackPtr] = ++Index;
      if (CallStackPtr == 0) {
        std::stringstream s;
        s << Index;
        Registers["SCS"] = s.str(); 
      }
	    CallStackPtr++;
      if (CallStackPtr > _StackDepth) {
		    std::cout << "Call Stack Too Deep!";
		    exit(-1);
	    }
      std::string token;
      token = str;
      Index = Labels[trim(token)];
      executeLine(Source[Index]);
	  }
	  else if (!strcasecmp("ButtonPress",ev))
	  {
	    myfile >> b;
	    std::cout << "ButtonPress: " << b << std::endl;
	    XTestFakeButtonEvent ( GlobalDisplay, b, True, Delay );
	  }
	  else if (!strcasecmp("Down",ev))
	  {
	    b = 1;
	    std::cout << "Down: " << b << std::endl;
	    XTestFakeButtonEvent ( GlobalDisplay, b, True, Delay );
	  }
	  else if (!strcasecmp("click",ev))
	  {
	    b = 1;
	    XTestFakeButtonEvent ( GlobalDisplay, b, True, Delay );
	    XFlush ( GlobalDisplay );
	    usleep(200000);
	    XTestFakeButtonEvent ( GlobalDisplay, b, False, Delay );
	  }
	  else if (!strcasecmp("ButtonRelease",ev))
	  {
	    myfile >> b;
	    std::cout << "ButtonRelease: " << b << std::endl;
	    XTestFakeButtonEvent ( GlobalDisplay, b, False, Delay );
	  }
	  else if (!strcasecmp("Up",ev))
	  {
	    b = 1;
	    std::cout << "Up: " << b << std::endl;
	    XTestFakeButtonEvent ( GlobalDisplay, b, False, Delay );
	  }
	  else if (!strcasecmp("Move",ev))
	  {
	    myfile >> x >> y;
	    std::cout << "Move: " << x << " " << y << std::endl;
	    XTestFakeMotionEvent ( GlobalDisplay, GlobalScreen , scale ( x ), scale ( y ), MouseDelay ); 
	  }
	  else if (!strcasecmp("RelativeMove",ev))
    {
      myfile >> x >> y;
      std::cout << "Move: " << x << " " << y << std::endl;
      XTestFakeRelativeMotionEvent ( GlobalDisplay, scale ( x ), scale ( y ), MouseDelay ); 
    }
	  else if (!strcasecmp("MotionNotify",ev) || !strcasecmp("Move",ev))
	  {
	    myfile >> x >> y;
	    std::cout << "MotionNotify: " << x << " " << y << std::endl;
	    XTestFakeMotionEvent ( GlobalDisplay, GlobalScreen , scale ( x ), scale ( y ), MouseDelay ); 
	  }
	  else if (!strcasecmp("KeyCodePress",ev))
	  {
	    myfile >> kc;
	    std::cout << "KeyPress: " << kc << std::endl;
	    XTestFakeKeyEvent ( GlobalDisplay, kc, True, KeyPressDelay );
	  }
	  else if (!strcasecmp("KeyCodeRelease",ev))
	  {
	    myfile >> kc;
	    std::cout << "KeyRelease: " << kc << std::endl;
    	  XTestFakeKeyEvent ( GlobalDisplay, kc, False, KeyPressDelay );
	  }
	  else if (!strcasecmp("KeySym",ev))
	  {
	    myfile >> ks;
	    std::cout << "KeySym: " << ks << std::endl;
	    if ( ( kc = XKeysymToKeycode ( GlobalDisplay, ks ) ) == 0 )
	    {
	    	std::cerr << "No keycode on remote display found for keysym: " << ks << std::endl;
        return;
	    }
	    XTestFakeKeyEvent ( GlobalDisplay, kc, True, KeyPressDelay );
	    XFlush ( GlobalDisplay );
	    XTestFakeKeyEvent ( GlobalDisplay, kc, False, Delay );
	  }
	  else if (!strcasecmp("KeySymPress",ev))
	  {
	    myfile >> ks;
	    std::cout << "KeySymPress: " << ks << std::endl;
	    if ( ( kc = XKeysymToKeycode ( GlobalDisplay, ks ) ) == 0 )
	    {
	    	std::cerr << "No keycode on remote display found for keysym: " << ks << std::endl;
        return;
	    }
	    XTestFakeKeyEvent ( GlobalDisplay, kc, True, KeyPressDelay );
	  }
	  else if (!strcasecmp("KeySymRelease",ev))
	  {
	    myfile >> ks;
	    std::cout << "KeySymRelease: " << ks << std::endl;
	    if ( ( kc = XKeysymToKeycode ( GlobalDisplay, ks ) ) == 0 )
	    {
	    	std::cerr << "No keycode on remote display found for keysym: " << ks << std::endl;
        return;
	    }
    	  XTestFakeKeyEvent ( GlobalDisplay, kc, False, KeyPressDelay );
	  }
	  else if (!strcasecmp("KeyStr",ev))
	  {
	    myfile >> ev;
	    std::cout << "KeyStr: " << ev << std::endl;
	    ks=XStringToKeysym(ev);
	    if ( ( kc = XKeysymToKeycode ( GlobalDisplay, ks ) ) == 0 )
	    {
	    	std::cerr << "No keycode on remote display found for '" << ev << "': " << ks << std::endl;
        return;
	    }
	    XTestFakeKeyEvent ( GlobalDisplay, kc, True, KeyPressDelay );
	    XFlush ( GlobalDisplay );
	    XTestFakeKeyEvent ( GlobalDisplay, kc, False, KeyPressDelay );
	  }
	  else if (!strcasecmp("KeyStrPress",ev))
	  {
	    myfile >> ev;
	    std::cout << "KeyStrPress: " << ev << std::endl;
	    ks=XStringToKeysym(ev);
	    if ( ( kc = XKeysymToKeycode ( GlobalDisplay, ks ) ) == 0 )
	    {
	    	std::cerr << "No keycode on remote display found for '" << ev << "': " << ks << std::endl;
        return;
	    }
	    XTestFakeKeyEvent ( GlobalDisplay, kc, True, KeyPressDelay );
	  }
	  else if (!strcasecmp("KeyStrRelease",ev))
	  {
	    myfile >> ev;
	    std::cout << "KeyStrRelease: " << ev << std::endl;
	    ks=XStringToKeysym(ev);
	    if ( ( kc = XKeysymToKeycode ( GlobalDisplay, ks ) ) == 0 )
	    {
	    	std::cerr << "No keycode on remote display found for '" << ev << "': " << ks << std::endl;
        return;
	    }
    	  XTestFakeKeyEvent ( GlobalDisplay, kc, False, KeyPressDelay );
	  }
	  else if (!strcasecmp("Send",ev))
	  {
	    myfile.ignore().get(str,1024);
	    b=0;
	    while(str[b]) sendChar(GlobalDisplay, str[b++]);
	  }
	  else if (!strcasecmp("Exec",ev))
	  {
	    myfile.ignore().get(str,1024);
	    pid_t cpid;
      cpid = fork();
      if (cpid==0) {
	      system(str);
	      exit(0);
	    } else {
        
      }
	  }
	  else if (!strcasecmp("MoveWindow",ev))
    {
      boost::regex expr("'(.*)',([\\s\\d]+),([\\s\\d]+)");
      boost::smatch what;
      myfile.ignore().get(str,1024);
      std::string s_str = trimWhitespace(str);
      if (boost::regex_match(s_str,what,expr)) {
        int x = stringToInt(what[2]);
        int y = stringToInt(what[3]);
        std::string name = what[1];
        std::cout << "MoveWindow " << name << " " << x << " " << y << std::endl;
        Window w = GetWindowByName(name);
      } //end if regex match
      
    }
	  else if (!strcasecmp("Focus",ev))
	  {
	    myfile.ignore().get(str,1024);
	    std::string s_str = trimWhitespace(str);
	    if (s_str.length() < 3) {
        return;
	    }
	    std::cout << "Focus: " << str << std::endl;
	    Window window, rootwindow;
      rootwindow = RootWindow(GlobalDisplay,DefaultScreen(GlobalDisplay));
      Atom atom = XInternAtom(GlobalDisplay, "_NET_CLIENT_LIST", True);
      XWindowAttributes attr;
      Atom atom_event;
      XEvent xev;
      Atom actualType;
      int format;
      unsigned long numItems, bytesAfter;
      unsigned char *data = 0;
      int status = XGetWindowProperty(GlobalDisplay,
                                      rootwindow,
                                      atom,
                                      0L,
                                      (~0L),
                                      false,
                                      AnyPropertyType,
                                      &actualType,
                                      &format,
                                      &numItems,
                                      &bytesAfter,
                                      &data);
      if (status >= Success && numItems) {
        std::cout << "Yay";
        int * array = (int *)data;
        for (int k = 0; k < numItems; k++) {
          window = (Window)array[k];
          char * name;
          XFetchName(GlobalDisplay,window,&name);
          if (name) {
            std::string s_name = name;
            if (s_name.find(s_str) != std::string::npos) {
              std::cout << "Raising Window";
              atom_event = XInternAtom (GlobalDisplay, "_NET_ACTIVE_WINDOW", False);
              xev.xclient.type = ClientMessage;
              xev.xclient.serial = 0;
              xev.xclient.send_event = True;
              xev.xclient.display = GlobalDisplay;
              xev.xclient.window = window;
              xev.xclient.message_type = atom_event;
              xev.xclient.format = 32;
              xev.xclient.data.l[0] = 2;
              xev.xclient.data.l[1] = 0;
              xev.xclient.data.l[2] = 0;
              xev.xclient.data.l[3] = 0;
              xev.xclient.data.l[4] = 0;
              XGetWindowAttributes(GlobalDisplay, window, &attr);
              XSendEvent (GlobalDisplay,
                          attr.root, False,
                          SubstructureRedirectMask | SubstructureNotifyMask,
                          &xev);
              XRaiseWindow(GlobalDisplay,window);
              XFlush ( GlobalDisplay );
            }
          }
        }
      } else {
        std::cout << "Failed...";
      }
	  } else if (ev[0]!=0) {
      std::string token = ev;
      if ( Labels[trim(token)] ) {
        CallStack[CallStackPtr] = ++Index;
        if (CallStackPtr == 0) {
          std::stringstream s;
          s << Index;
          Registers["SCS"] = s.str(); 
        }
        CallStackPtr++;
        if (CallStackPtr > _StackDepth) {
          std::cout << "Call Stack Too Deep!";
          exit(-1);
        }
        Index = Labels[trim(token)];
        executeLine(Source[Index]);
      }
    }

	  // sync the remote server
	  XFlush ( GlobalDisplay );
    myfile.clear();

}
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * We need to have the entire file as a logical structure instead of just
 * a stream of bits because we need to move back and forth in the file and
 * using tellg just isn't cutting it. 
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
void parseFileIntoStruct(char * fileName) {
//   std::cout << "fileName is : " << fileName;
  std::string line;
  std::ifstream file (fileName);
  std::string token;
  std::stringstream ss;
  int index = 0;
  while( getline(file,line) ) {
//     std::cout << "CURRENT LINE IS: " << line << std::endl;
    trim(line);
//     std::cout << "trimmed line is: " << line << std::endl;
    if (line.empty() || line == "" || line.substr(0,1) == "#") {
//       std::cout << " line is blank" << std::endl;
      continue;
    } else {
//       std::cout << " line is not blank" << std::endl;
    }
//     std::cout << index << " > " << _SourceLines << std::endl;
    if (index >= _SourceLines) {
//       std::cout << "Calculating new lines" << std::endl;
      int _NewSourceLines = _SourceLines + floor(_SourceLines * 0.33);
//       std::cout << "new sl " << _NewSourceLines << std::endl;
      std::string * NewSource = new std::string[_NewSourceLines];
      memcpy(NewSource,Source,_SourceLines * sizeof(std::string));
      Source = NewSource;
      _SourceLines = _NewSourceLines;
    } else {
//       std::cout << "no need to resize" << std::endl;
    }
//     std::cout << " setting line " << std::endl;
    Source[index] = line;
//     std::cout << " line set " << std::endl;
    // Now let's look at the string and find out some stuff about it
//     std::cout << "setting ss" << std::endl;
    ss.str(line);
//     std::cout << "reading token" << std::endl;
    ss >> token;
//     std::cout << "token read" << std::endl;
    if (!token.empty() && token.length() > 2) {
//       std::cout << "token is not empty " << token << token.length() << std::endl;
      token = trim(token);
      if (token == "label" || token == "function") {
        ss >> token;
        Labels[token] = index + 1; // i.e. we goto the declaration line + 1
      } else if (token == "entry" || token == "main") {
        Entry = index;
      }
//       std::cout << "token looking complete" << std::endl;
    } else {
//       std::cout << "token was empty" << std::endl;
    }
    ss.clear();
    index++;
  }
//   std::cout << "done parsing file" << std::endl;
  SourceNumLines = index;
  if (Entry == NULL) {
    // we assume the first line is the entry
    Entry = 0;
  }
}

/****************************************************************************/
/*! Main event-loop of the application. Loops until a key with the keycode
    \a QuitKey is pressed. Sends all mouse- and key-events to the remote
	display.

    \arg Display * RemoteDpy - used display.
	\arg int RemoteScreen - the used screen.
*/
/****************************************************************************/

void eventLoop (Display * RemoteDpy, int RemoteScreen,char * filename) {
//   std::cout << "eventLoop beginning with filename " << filename << std::endl;
  parseFileIntoStruct(filename);
  GlobalDisplay = RemoteDpy;
  GlobalScreen = RemoteScreen;
  for ( Index = Entry; Index <= SourceNumLines; Index++ ) {
    //usleep(500000);
//     std::cout << "\t\t\t\t\tLine: " << Source[Index] << std::endl;
    if (isPostIf(Source[Index])) {
      //do nothing
    } else {
        executeLine(Source[Index]);
    }
  } // end for index 
}


/****************************************************************************/
/*! Main function of the application. It expects no commandline arguments.

    \arg int argc - number of commandline arguments.
	\arg char * argv[] - vector of the commandline argument strings.
*/
/****************************************************************************/
int main (int argc, char * argv[]) {

  // parse commandline arguments
  parseCommandLine ( argc, argv );
  
  // open the remote display or abort
  Display * RemoteDpy = remoteDisplay ( Remote );

  // get the screens too
  int RemoteScreen = DefaultScreen ( RemoteDpy );
  
  XTestDiscard ( RemoteDpy );

  // start the main event loop
//   std::cout << "Starting main loop" << std::endl;
  eventLoop ( RemoteDpy, RemoteScreen, argv[2] );

  // discard and even flush all events on the remote display
  XTestDiscard ( RemoteDpy );
  XFlush ( RemoteDpy ); 

  // we're done with the display
  XCloseDisplay ( RemoteDpy );

  std::cerr << PROG << ": pointer and keyboard released. " << std::endl;
  
  // go away
  exit ( EXIT_SUCCESS );
}





