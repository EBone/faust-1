/************************************************************************
 ************************************************************************
 FAUST compiler
 Copyright (C) 2009 GRAME, Centre National de Creation Musicale
 ---------------------------------------------------------------------
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/



/*****************************************************************************
 ******************************************************************************
 
 
						The Documentator Language
 
 
 ******************************************************************************
 *****************************************************************************/


/**
 * @file doc.cpp
 * @author Karim Barkati and Yann Orlarey
 * @version 1.0
 * @date 2009
 * @brief Implementation of documentation trees support and printing.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <algorithm>
#include <functional>

#include <iostream>
#include <fstream>
#include <sstream>

#include <map>
#include <string>
#include <vector>

#include "ppbox.hh"
#include "prim2.hh"
#include "doc.hh"
#include "eval.hh"
#include "errormsg.hh"
#include "doc_Text.hh"
#include "sigprint.hh"
#include "propagate.hh"
#include "doc_compile.hh"
#include "enrobage.hh"
#include "drawschema.hh"
#include "names.hh"
#include "simplify.hh"
#include "privatise.hh"
#include "recursivness.hh"
#include "lateq.hh"
//#include "doc_sharing.hh"
#include "sourcereader.hh"
#include "notice.hh"
#include "compatibility.hh"



#define MAXIDCHARS 5				///< max numbers (characters) to represent ids (e.g. for directories).

using namespace std ;


/*****************************************************************************
						Globals and prototyping
 *****************************************************************************/

extern Tree						gExpandedDefList;
extern map<Tree, set<Tree> > 	gMetaDataSet;
extern bool            			gDetailsSwitch;
extern bool            			gStripDocSwitch;
extern string					gFaustDirectory;
extern string					gFaustSuperDirectory;
extern string					gFaustSuperSuperDirectory;
extern string					gMasterDocument;
extern SourceReader				gReader;

extern string 					gDocName;				///< Contains the filename for out documentation.
static const char* 				gDocDevSuffix;			///< ".tex" (or .??? - used to choose output device).
static string 					gCurrentDir;			///< Room to save current directory name.
static const string				gLatexheaderfilename = "latexheader.tex";

vector<Tree> 					gDocVector;				///< Contains <doc> parsed trees: DOCTXT, DOCEQN, DOCDGM.

static struct tm				gCompilationDate;



/* Printing functions */
static void		printdocheader(ostream& docout);
static void		printlatexheader(istream& latexheader, ostream& docout);
static void		printfaustlistings(ostream& docout);
static void		printfaustlisting(string& path, ostream& docout);
static void		printlatexfooter(ostream& docout);
static void		printdoccontent(const char* svgTopDir, const vector<Tree>& docVector, const string& faustversion, ostream& docout);
static void		printfaustdocstamp(const string& faustversion, ostream& docout);
static void		printDocEqn(Lateq* ltq, ostream& docout);
static void		printDocDgm(const Tree expr, const char* svgTopDir, ostream& docout, int i);

/* Primary sub-functions for <equation> handling */
static void	prepareDocEqns( const vector<Tree>& docBoxes, vector<Lateq*>& docCompiledEqnsVector );		///< Caller function.
static void	collectDocEqns( const vector<Tree>& docBoxes, vector<Tree>& eqBoxes );						///< step 0. Feed doceqnInfosVector.
static void	mapEvalDocEqn( const vector<Tree>& eqBoxes, const Tree& env, vector<Tree>& evalEqBoxes );	///< step 1. Evaluate boxes.
static void	mapGetEqName( const vector<Tree>& evalEqBoxes, vector<string>& eqNames );					///< step 2. Get boxes name.
static void	calcEqnsNicknames( const vector<string>& eqNames, vector<string>& eqNicknames );			///< step 3. Calculate nicknames.
static void	mapPrepareEqSig( const vector<Tree>& evalEqBoxes, vector<int>& eqInputs, vector<int>& eqOutputs, vector<Tree>& eqSigs );	///< step 4&5. Propagate and prepare signals.
static void	mapSetSigNickname( const vector<string>& eqNicknames, const vector<int>& eqInputs, const vector<Tree>& eqSigs );	///< step 6. Set signals nicknames.
static void	collectEqSigs( const vector<Tree>& eqSigs, Tree& superEqList );								///< step 7. Collect all signals in a superlist.
static void	annotateSuperList( DocCompiler* DC, Tree superEqList );								///< step 8. Annotate superlist.
//static void	calcAndSetLtqNames( Tree superEqList );		///< step 9. 
static void	mapCompileDocEqnSigs( const vector<Tree>& eqSigs, const vector<int>& eqInputs, const vector<int>& eqOutputs, DocCompiler* DC, vector<Lateq*>& docCompiledEqnsVector );	///< step 10. Compile equations.

/* Secondary sub-functions for <equation> handling */
static string	calcNumberedName(const char* base, int i);
static void		getBoxInputsAndOutputs(const Tree t, int& numInputs, int& numOutputs);
static string	calcDocEqnInitial(const string s);

/* Notice related functions */
static void		initCompilationDate();
static struct tm* getCompilationDate();

/* Files functions */
static int		cholddir ();
static int		mkchdir(const char* dirname);
static int		makedir(const char* dirname);
static void		getCurrentDir();
static istream* openArchFile (const string& filename);
static char*	legalFileName(const Tree t, int n, char* dst);
static string	rmExternalDoubleQuotes(const string& s);
static void		copyFaustSources(const char* projname, const vector<string>& pathnames);
static void		declareAutoDoc();



/*****************************************************************************
					Types of Documentation Elements
 *****************************************************************************/

Sym DOCTXT = symbol ("DocTxt");
Tree docTxt(const char* name)		{ return tree( DOCTXT, tree(symbol(name)) ); }
bool isDocTxt(Tree t)				{ return t->node() == Node(DOCTXT); }
bool isDocTxt(Tree t0, const char** str)
{
	Tree t1; Sym s;
	if ( isTree(t0, DOCTXT, t1) && isSym(t1->node(), &s) ) {
		*str = name(s);
		return true;
	} else {
		return false;
	}
}

Sym DOCEQN = symbol ("DocEqn");
Tree docEqn(Tree x) 				{ return tree(DOCEQN, x); 		}
bool isDocEqn(Tree t, Tree& x) 		{ return isTree(t, DOCEQN, x); 	}

Sym DOCDGM = symbol ("DocDgm");
Tree docDgm(Tree x) 				{ return tree(DOCDGM, x); 		}
bool isDocDgm(Tree t, Tree& x) 		{ return isTree(t, DOCDGM, x); 	}

Sym DOCNTC = symbol ("DocNtc");
Tree docNtc()						{ return tree(DOCNTC);			}
bool isDocNtc(Tree t)				{ return isTree(t, DOCNTC); 	}

Sym DOCLST = symbol ("DocLst");
Tree docLst()						{ return tree(DOCLST);			}
bool isDocLst(Tree t)				{ return isTree(t, DOCLST); 	}


//string getDocTxt(Tree t) 			{ return hd(t)->branch(0); }



/*****************************************************************************
				Main Printing Function for the Documentation
 *****************************************************************************/


/**
 * @brief The entry point to generate faust doc files.
 *
 * The entry point to generate the output LaTeX file, stored in the directory "<projname>-math/".
 * This file eventually references images for diagrams, generated in SVG subdirectories.
 * The device system was adapted from drawSchema's device system.
 *
 * @param[in]	projname		Basename of the new doc directory ("*-math").
 * @param[in]	docdev			The doc device; only ".tex" is supported for the moment.
 * @param[in]	faustversion	The current version of this Faust compiler.
 */
void printDoc(const char* projname, const char* docdev, const char* faustversion)
{
	gDocDevSuffix = docdev;
	
	/** File stuff : create doc directories and a tex file. */
	//cerr << "Documentator : printDoc : gFaustDirectory = '" << gFaustDirectory << "'" << endl;
	//cerr << "Documentator : printDoc : gFaustSuperDirectory = '" << gFaustSuperDirectory << "'" << endl;
	//cerr << "Documentator : printDoc : gFaustSuperSuperDirectory = '" << gFaustSuperSuperDirectory << "'" << endl;
	//cerr << "Documentator : printDoc : gCurrentDir = '" << gCurrentDir << "'" << endl;
	
	//cerr << "Documentator : printDoc : Creating directory '" << projname << "'" << endl;
	makedir(projname); 			// create a top directory to store files
	
	string svgTopDir = subst("$0/svg", projname);
	//cerr << "Documentator : printDoc : Creating directory '" << svgTopDir << "'" << endl;
	makedir(svgTopDir.c_str()); // create a directory to store svg-* subdirectories.
	
	string cppdir = subst("$0/cpp", projname);
	//cerr << "Documentator : printDoc : Creating directory '" << cppdir << "'" << endl;
	makedir(cppdir.c_str());	// create a cpp directory.
	
	string pdfdir = subst("$0/pdf", projname);
	//cerr << "Documentator : printDoc : Creating directory '" << pdfdir << "'" << endl;
	makedir(pdfdir.c_str());	// create a pdf directory.
	
	/* Copy all Faust source files into an 'src' sub-directory. */
	vector<string> pathnames = gReader.listSrcFiles();
	copyFaustSources(projname, pathnames);
	
	string texdir = subst("$0/tex", projname);
	//cerr << "Documentator : printDoc : Creating directory '" << texdir << "'" << endl;
	mkchdir(texdir.c_str()); 	// create a directory and move into.
	//cerr << "Documentator : printDoc : Creating file '" << subst("$0.$1", gDocName, docdev) << "' in '" << texdir << "'" << endl;

	 /** Create the doc file. */
	ofstream docout(subst("$0.$1", gDocName, docdev).c_str());
	cholddir();					// return to current directory
	
	/** Simulate a default doc if no <doc> tag detected. */
	if (gDocVector.empty()) { declareAutoDoc(); } 
	
	/** 
	 * Notice's stuff. 
	 * @todo	Future place to load translation files. 
	 */
	initDocNotice();
//	enum { langFR, langIT };
//	switch(gDocLang) {
//		case langFR:
//			loadDocNoticeFile("notice-fr.txt");
//			break;
//		case langIT:
//			loadDocNoticeFile("notice-it.txt");
//			break;
//	}
	
	
	/** Printing stuff : in the '.tex' ouptut file, eventually including SVG files. */
	printfaustdocstamp(faustversion, docout);						///< Faust version and compilation date (comment).
	istream* latexheader = openArchFile(gLatexheaderfilename);
	printlatexheader(*latexheader, docout);							///< Static LaTeX header (packages and setup).
	printdocheader(docout);											///< Dynamic visible header (maketitle).
	printdoccontent(svgTopDir.c_str(), gDocVector, faustversion, docout);		///< Generate math contents (main stuff!).
	printlatexfooter(docout);										///< Static LaTeX footer.
	
}



/*****************************************************************************
			LaTeX basic printing functions of the Documentation
 *****************************************************************************/

/** 
 * Print a static LaTeX header. 
 *
 * @param[in]	latexheader	The file containing the static part of the LaTeX header.
 * @param[out]	docout		The LaTeX output file to print into.
 */
static void printlatexheader(istream& latexheader, ostream& docout)
{	
	string	s;
	while(getline(latexheader, s)) docout << s << endl;
}

/** 
 * Print the dynamic visible header, in a LaTeX "tabular" environment. 
 *
 * @param[out]	docout		The LaTeX output file to print into.
 */
static void printdocheader(ostream& docout)
{
    /* Defines the metadata we want to print as comments at the begin of the LaTeX file. */
    set<Tree> selectedKeys;
    selectedKeys.insert(tree("name"));
    selectedKeys.insert(tree("author"));
    selectedKeys.insert(tree("copyright"));
    selectedKeys.insert(tree("license"));
    selectedKeys.insert(tree("version"));
	
	string title = "Documentation"; ///< Default title.
	if (gMetaDataSet.count(tree("name"))) {
		title = unquote( tree2str(*(gMetaDataSet[tree("name")].begin())) ); 
	}
	docout << "\\title{" << title << "}" << endl;
	
	string author = "(anonymous)"; ///< Default author.
	if (gMetaDataSet.count(tree("author"))) {
		author = unquote( tree2str(*(gMetaDataSet[tree("author")].begin())) ); 
		docout << "\\author{" << author << "}" << endl;
	}
	
    docout << "\\date{\\today}" << endl << endl;
    docout << "\\maketitle" << endl << endl;
	
	
	if (! gMetaDataSet.empty()) {
		docout << "\\begin{tabular}{ll}" << endl;
		docout << "\t\\hline" << endl;
		for (map<Tree, set<Tree> >::iterator i = gMetaDataSet.begin(); i != gMetaDataSet.end(); i++) {
			if (selectedKeys.count(i->first)) {
				docout << "\t\\textbf{" << *(i->first);
				const char* sep = "} & ";
				for (set<Tree>::iterator j = i->second.begin(); j != i->second.end(); j++) {
					docout << sep << rmExternalDoubleQuotes(tree2str(*j));
					sep = ", ";
				}
				const char* sep2 = "\\\\";
				docout << sep2 << endl;
			}
		}
		docout << "\t\\hline\\\\" << endl;
		docout << "\\end{tabular}" << endl;
		docout << "\\bigskip" << endl << endl;
	}
}


/** 
 * Print listings of each Faust code ".dsp" files,
 * calling the 'printfaustlisting' function.
 *
 * @param[out]	docout		The LaTeX output file to print into.
 */
static void printfaustlistings(ostream& docout)
{	
	vector<string> pathnames = gReader.listSrcFiles();
	
	/* Listings printing. */
	for (unsigned int i=0; i< pathnames.size(); i++) {
		printfaustlisting(pathnames[i], docout);
	}
}


/** 
 * Print a listing of the Faust code, in a LaTeX "listing" environment.
 * Strip content of <doc> tags.
 *
 * @param[in]	faustfile	The source file containing the Faust code.
 * @param[out]	docout		The LaTeX output file to print into.
 */
static void printfaustlisting(string& faustfile, ostream& docout)
{	
	string	s;
	ifstream src;
	
	//cerr << "Documentator : printfaustlisting : Opening file '" << faustfile << "'" << endl;
	src.open(faustfile.c_str(), ifstream::in);
	
	docout << endl << "\\bigskip\\bigskip" << endl;
	docout << "\\begin{lstlisting}[caption=\\texttt{" << filebasename(faustfile.c_str()) << "}]" << endl;

	bool isInsideDoc = false;
	
	if (faustfile != "" && src.good()) {
		while(getline(src, s)) { /** We suppose there's only one <doc> tag per line. */
			size_t foundopendoc  = s.find("<doc>");
			if (foundopendoc != string::npos && gStripDocSwitch) isInsideDoc = true;
			
			if (isInsideDoc == false)
				docout << s << endl;
			
			size_t foundclosedoc = s.find("</doc>");
			if (foundclosedoc != string::npos && gStripDocSwitch) isInsideDoc = false;
		}
	} else {
		cerr << "ERROR : can't open faust source file " << faustfile << endl;
		exit(1);
	}
	
	docout << "\\end{lstlisting}" << endl << endl;
}


/** 
 * Print the static LaTeX footer. 
 *
 * @param[out]	docout		The LaTeX output file to print into.
 */
static void printlatexfooter(ostream& docout)
{
	docout << endl << "\\end{document}" << endl << endl;
}


/** 
 * Print a "doc stamp" in the LaTeX document :
 * - the Faust version,
 * - the date of doc compilation,
 * - faust's web site URL.
 *
 * @param[in]	faustversion	The current version of this Faust compiler.
 * @param[out]	docout			The LaTeX output file to print into.
 */
static void printfaustdocstamp(const string& faustversion, ostream& docout)
{
	char datebuf [150];
	strftime (datebuf, 150, "%c", getCompilationDate());
	
	docout << "%% This documentation was generated with Faust version " << faustversion << endl;
	docout << "%% " << datebuf << endl;
	docout << "%% http://faust.grame.fr" << endl << endl;
}


/** 
 * @brief Declare an automatic documentation.
 *
 * This function simulates a default documentation : 
 * if no <doc> tag was found in the input faust file,
 * and yet the '-math' option was called, 
 * then print a complete 'process' doc. 
 */
static void declareAutoDoc() 
{
	Tree autodoc = nil;
	Tree process = boxIdent("process");
	
	string autoEquationTxt = "\n\\section{Equations of process}\n\n";
	autoEquationTxt += "This program calls a \\emph{process}, which mathematical description follows:\n";
	autodoc = cons(docTxt(autoEquationTxt.c_str()), autodoc);
	autodoc = cons(docEqn(process), autodoc);
	
	string autoDiagramTxt = "\n\\section{Block-diagram schema of process}\n\n";
	autoDiagramTxt += "The block-diagram schema of \\emph{process} is shown on figure \\ref{figure1}.\n";
	autodoc = cons(docTxt(autoDiagramTxt.c_str()), autodoc);
	autodoc = cons(docDgm(process), autodoc);	
	
	string autoNoticeTxt = "\n\\section{Notice of this documentation}\n\n";
	autoNoticeTxt += "You might be careful of certain information and naming conventions used in this documentation:\n";
	autodoc = cons(docTxt(autoNoticeTxt.c_str()), autodoc);
	autodoc = cons(docNtc(), autodoc);
	
	string autoListingTxt;
	vector<string> pathnames = gReader.listSrcFiles();
	if(pathnames.size() > 1) {
		autoListingTxt = "\n\\section{Complete listings of the input code}\n\n";
		autoListingTxt += "The following listings show the Faust code parsed to compile this documentation.\n";
	} else {
		autoListingTxt = "\n\\section{Complete listing of the input code}\n\n";
		autoListingTxt += "The following listing shows the Faust code parsed to compile this documentation.\n";
	}
	autodoc = cons(docTxt(autoListingTxt.c_str()), autodoc);
	autodoc = cons(docLst(), autodoc);

	declareDoc(autodoc);
}



/*****************************************************************************
			Main loop : launches prepare, evaluate, and print functions
 *****************************************************************************/

/**
 * @brief Main documentator loop.
 *
 * First loop on gDocVector, which contains the faust <doc> trees.
 * Second loop for each of these <doc> trees, which contain parsed input expressions of 3 types :
 * DOCEQN for <equation> tags, DOCDGM for <diagram> tags, and DOCTXT for direct LaTeX text (no tag).
 * - DOCTXT expressions printing is trivial.
 * - DOCDGM expressions printing calls 'printDocDgm' to generate SVG files and print LaTeX "figure" code.
 * - DOCEQN expressions printing calls 'printDocEqn' after an important preparing work 
 *   has been done by 'prepareDocEqns'.
 *
 * @param[in]	projname		Basename of the new doc directory ("*-math").
 * @param[in]	docVector		Contains all <doc> parsed content (as boxes).
 * @param[in]	faustversion	The current version of this Faust compiler.
 * @param[out]	docout			The output file to print into.
 **/
static void printdoccontent(const char* svgTopDir, const vector<Tree>& docVector, const string& faustversion, ostream& docout)
{
	//cerr << endl << "Documentator : printdoccontent : " << docVector.size() << " <doc> tags read." << endl;
	
	/** Equations need to be prepared (named and compiled) before printing. */
	vector<Lateq*>  docCompiledEqnsVector;
	prepareDocEqns( docVector, docCompiledEqnsVector ); ///< Quite a lot of stuff there.
	
	vector<Lateq*>::iterator eqn_it = docCompiledEqnsVector.begin();
	int i = 1;			///< For diagram directories numbering.

	/** First level printing loop, on docVector. */
	for (vector<Tree>::const_iterator doc=docVector.begin(); doc<docVector.end(); doc++) {
		
		Tree L = reverse(*doc);
		//cerr << "Entering into <doc> parsing..." << endl; 
		
		/** Second level printing loop, on each <doc>. */
		while (isList(L)) {
			Tree expr;
			if ( isDocEqn(hd(L), expr) ) { ///< After equations are well prepared and named.
				printDocEqn(*eqn_it++, docout);
			}
			else if ( isDocDgm(hd(L), expr) ) { 
				printDocDgm(expr, svgTopDir, docout, i++);
			}
			else if ( isDocTxt(hd(L)) ) { 
				docout << *hd(L)->branch(0) << endl; // Directly print registered doc text.
				static unsigned int i = 0;
				cerr << "docTxt" << ++i << " = \"" << *hd(L)->branch(0) << "\"" << endl; // Directly print registered doc text.
			}
			else if ( isDocNtc(hd(L)) ) { 
				printDocNotice(faustversion, docout);
			}
			else if ( isDocLst(hd(L)) ) { 
				printfaustlistings(docout);
			}
			else { 
				cerr << "ERROR : " << *hd(L) << " is not a valid documentation type." << endl; 
			}
			L = tl(L);
		}
		//cerr << " ...end of <doc> parsing." << endl; 
	}
}



/*****************************************************************************
			Primary sub-functions for <equation> handling
 *****************************************************************************/

/**
 * @brief Caller function for all steps of doc equations preparation.
 *  
 * Note : many of the functions called put their result into their last argument
 * in a "source / destination" manner, 
 * the "destination" being declared before the function call. 
 *
 * @param[in]	docBoxes				The <doc> boxes to collect and prepare.
 * @param[out]	docCompiledEqnsVector	The place to store compiled equations.
 */
static void prepareDocEqns(const vector<Tree>& docBoxes, vector<Lateq*>& docCompiledEqnsVector)
{	
	vector<Tree>	eqBoxes;		collectDocEqns( docBoxes, eqBoxes );		///< step 0. Feed doceqnInfosVector.
	vector<Tree>	evalEqBoxes;	mapEvalDocEqn( eqBoxes, gExpandedDefList, evalEqBoxes );	///< step 1. Evaluate boxes.
	vector<string>	eqNames;		mapGetEqName( evalEqBoxes, eqNames );		///< step 2. Get boxes name.
	vector<string>	eqNicknames;	calcEqnsNicknames( eqNames, eqNicknames );	///< step 3. Calculate nicknames.
	
	vector<int>		eqInputs;
	vector<int>		eqOutputs;
	vector<Tree>	eqSigs;			mapPrepareEqSig( evalEqBoxes, eqInputs, eqOutputs, eqSigs );	///< step 4&5. Propagate and prepare signals.
	mapSetSigNickname( eqNicknames, eqInputs, eqSigs );									///< step 6. Set signals nicknames.
	Tree			superEqList;	collectEqSigs( eqSigs, superEqList );		///< step 7. Collect all signals in a superlist.
	
	DocCompiler* DC = new DocCompiler(0, 0);
	annotateSuperList( DC, superEqList );										///< step 8. Annotate superEqList.
	//calcAndSetLtqNames( superEqList );										///< step 9. (directly in 10.)
	mapCompileDocEqnSigs( eqSigs, eqInputs, eqOutputs, DC, docCompiledEqnsVector );		///< step 10. Compile every signal.
}


/**
 * #0. Collect every <equation> found in all <doc> faust comments.
 *
 * @param[in]	docBoxes	The <doc> boxes to filter.
 * @param[out]	eqBoxes		The place to store only <equation> boxes.
 */
static void collectDocEqns(const vector<Tree>& docBoxes, vector<Tree>& eqBoxes)
{
	int nbdoceqn = 0;
	
	for (vector<Tree>::const_iterator doc=docBoxes.begin(); doc<docBoxes.end(); doc++) {
		Tree L = reverse(*doc);
		Tree expr;
		while (isList(L)) {
			if ( isDocEqn(hd(L), expr) ) {
				eqBoxes.push_back(expr);
				nbdoceqn++;
			}
			L = tl(L);
		}
	}
	//cerr << "Documentator : collectDocEqns : " << nbdoceqn << " <equation> tags found." << endl;
}


/**
 * #1. Evaluate every doc <equation> (evaluation replaces abstractions by symbolic boxes).
 *
 * @param[in]	eqBoxes		The boxes to evaluate.
 * @param[in]	env			The environment for the evaluation.
 * @param[out]	evalEqBoxes	The place to store evaluated equations boxes.
 */
static void mapEvalDocEqn(const vector<Tree>& eqBoxes, const Tree& env, vector<Tree>& evalEqBoxes)
{
	//cerr << "###\n# Documentator : mapEvalDocEqn" << endl;
	
	for ( vector<Tree>::const_iterator eq=eqBoxes.begin(); eq < eqBoxes.end(); eq++)
	{
		evalEqBoxes.push_back(evaldocexpr( *eq, env ));
	}
	//cerr << "Documentator : end of mapEvalDocEqn\n---" << endl;
}


/**
 * #2. Get name if exists, else create one, and store it.
 *
 * @param[in]	evalEqBoxes	Evaluated box trees, eventually containing an equation name.
 * @param[out]	eqNames		The place to store equations names.
 */
static void mapGetEqName(const vector<Tree>& evalEqBoxes, vector<string>& eqNames)
{
	//cerr << "###\n# Documentator : mapGetEqName" << endl;
	
	int i = 1;
	for( vector<Tree>::const_iterator eq = evalEqBoxes.begin(); eq < evalEqBoxes.end(); eq++, i++ ) {
		Tree id;
		string s;
		int n,m; getBoxType(*eq, &n, &m); // eq name only for bd without inputs
		if ( n==0 && getDefNameProperty(*eq, id) ) {
			s = tree2str(id);
		}
		else {
			s = calcNumberedName("doceqn-", i);
		}		
		eqNames.push_back( s ) ;
	}
	//cerr << "Documentator : end of mapGetEqName\n---" << endl;
}


/**
 * #3. Calculate a nickname for each equation and store it.
 *
 * @param[in]	eqNames		Equations names to parse.
 * @param[out]	eqNicknames	The place to store calculated nicknames.
 *
 * @todo Should check unicity : check whether several names share the same initial,
 * or else capture consonants for example.
 */
static void calcEqnsNicknames(const vector<string>& eqNames, vector<string>& eqNicknames)
{
	//cerr << "###\n# Documentator : calcEqnsNicknames" << endl;
	
	vector<string> v;
	
	for( vector<string>::const_iterator eq = eqNames.begin(); eq < eqNames.end(); eq++ ) {
		string init = calcDocEqnInitial(*eq);
		v.push_back(init);
		/** Check duplicates */
//		for( vector<string>::iterator it = v.begin(); it < v.end()-1; ++it ) {
//			if (init == *it) {
//				//cerr << "!! Warning Documentator : calcEqnsNicknames : duplicates \"" << init << "\"" << endl;
//			}
//		}
		eqNicknames.push_back(init);
	}
	
//	for( vector<string>::const_iterator eq = eqNames.begin(); eq < eqNames.end(); eq++ ) {
//		int c = 0;
//		c = count_if(eqNames.begin(), eqNames.end(), bind2nd(equal_to<string>(), *eq));
//		if (c > 0) { 
//			cerr << "- Duplicate nickname !! " << *eq << endl; 
//		} else {
//			cerr << "(no duplicate) " << *eq << endl;
//		}
//	}
	
	//cerr << "Documentator : end of calcEqnsNicknames\n---" << endl;
}


/**
 * #4&5. Propagate and prepare every doc <equation>.
 *
 * Call boxPropagateSig, deBruijn2Sym, simplify, and privatise.
 *
 * @param[in]	evalEqBoxes		Equations boxes to propagate as signals.
 * @param[out]	eqSigs			The place to store prepared signals.
 */
static void mapPrepareEqSig(const vector<Tree>& evalEqBoxes, vector<int>& eqInputs, vector<int>& eqOutputs, vector<Tree>& eqSigs)
{
	//cerr << "###\n# Documentator : mapPrepareEqSig" << endl;
	
	for( vector<Tree>::const_iterator eq = evalEqBoxes.begin(); eq < evalEqBoxes.end(); eq++ ) {
		
		int numInputs, numOutputs;
		getBoxInputsAndOutputs(*eq, numInputs, numOutputs);
		//cerr << numInputs <<" ins and " << numOutputs <<" outs" << endl;
		eqInputs.push_back(numInputs);
		eqOutputs.push_back(numOutputs);
		
		Tree lsig1 = boxPropagateSig( nil, *eq , makeSigInputList(numInputs) );
		//cerr << "output signals are : " << endl;  printSignal(lsig1, stderr);
		
		Tree lsig2 = deBruijn2Sym(lsig1);   ///< Convert debruijn recursion into symbolic recursion
		Tree lsig3 = simplify(lsig2);		///< Simplify by executing every computable operation
		Tree lsig4 = privatise(lsig3);		///< Un-share tables with multiple writers
		
		eqSigs.push_back(lsig4);
	}
	//cerr << "Documentator : end of mapPrepareEqSig\n---" << endl;
}


/**
 * #6. Set signals nicknames.
 *
 * Do nothing for the moment !
 * @param[in]	eqNicknames		Contains previously calculated nicknames.
 * @param[out]	eqSigs			The signals to tag with a NICKNAMEPROPERTY.
 */
static void mapSetSigNickname(const vector<string>& eqNicknames, const vector<int>& eqInputs, const vector<Tree>& eqSigs)
{
	//cerr << "###\n# Documentator : mapSetSigNickname" << endl;

//	Do nothing for the moment...
//	for( unsigned int i=0; i < eqSigs.size(); i++ ) {
//		if (eqInputs[i] == 0) // Only "generators" should be finally named with user equation (nick)name.
//			setSigListNickName(eqSigs[i], eqNicknames[i]);
//	}
	//cerr << "Documentator : end of mapSetSigNickname\n---" << endl;
}


/**
 * #7. Collect each prepared list of signals to construct a super list.
 *
 * @param[in]	eqSigs			Contains well-prepared and nicknamed signals.
 * @param[out]	superEqList		The root where to 'cons' signals all together.
 */
static void collectEqSigs(const vector<Tree>& eqSigs, Tree& superEqList)
{
	//cerr << "###\n# Documentator : collectEqSigs" << endl;
	
	superEqList = nil;
	
	for( vector<Tree>::const_iterator it = eqSigs.begin(); it < eqSigs.end(); ++it ) {
		superEqList = cons( *it, superEqList );
	}
	//printSignal(superEqList, stdout, 0);
	
	//cerr << endl << "Documentator : end of collectEqSigs\n---" << endl;
}


/**
 * #8. Annotate superEqList (to find candidate signals to be named later).
 *
 * @param[in]	DC			The signals compiler.
 * @param[out]	superEqList	The super equations signal tree to annotate. 
 */
static void	annotateSuperList(DocCompiler* DC, Tree superEqList)
{
	DC->annotate(superEqList);
}


///**
// * #9. Calculated and set lateq (LaTeX equation) names.
// * Note : Transfered into mapCompileDocEqnSigs (DocCompiler::compileMultiSignal).
// */
//static void	calcAndSetLtqNames(Tree superEqList)
//{
//	
//}


/**
 * #10. Name and compile prepared doc <equation> signals.
 *
 * @param[in]	eqSigs					Contains well-prepared and nicknamed signals.
 * @param[in]	DC						The signals compiler.
 * @param[out]	docCompiledEqnsVector	The place to store each compiled Lateq* object.
 */
static void mapCompileDocEqnSigs(const vector<Tree>& eqSigs, const vector<int>& eqInputs, const vector<int>& eqOutputs, DocCompiler* DC, vector<Lateq*>& docCompiledEqnsVector)
{
	//cerr << "###\n# Documentator : mapCompileDocEqnSigs" << endl;
	
	for( unsigned int i=0; i < eqSigs.size(); i++ ) {
		
		//		docCompiledEqnsVector.push_back( DC->compileMultiSignal(*it, 0) );
		docCompiledEqnsVector.push_back( DC->compileLateq(eqSigs[i], new Lateq(eqInputs[i], eqOutputs[i])) );
	}
	
	//cerr << "Documentator : end of mapCompileDocEqnSigs\n---" << endl;
}



/*****************************************************************************
				Secondary sub-functions for <equation> handling
 *****************************************************************************/


/**
 * Calculate an appropriate nickname for equations,
 * from previous names.
 *
 * @param	The string to parse.
 * @return	Essentially returns the initial character, 
 * except "Y" for "process", and "Z" for unnamed equations.
 */
static string calcDocEqnInitial(const string s)
{
	string nn;
	if(s == "process")
		nn = "Y";
	else if (s.substr(0,6) == "doceqn")
		nn = "Z";
	else
		nn += toupper(s[0]);
	return nn;
}


/**
 * Just get the number of inputs and the number of outputs of a box.
 *
 * @param[in]	t			The box tree to get inputs and outputs from.
 * @param[out]	numInputs	The place to store the number of inputs.
 * @param[out]	numOutputs	The place to store the number of outputs.
 */
static void getBoxInputsAndOutputs(const Tree t, int& numInputs, int& numOutputs)
{
	if (!getBoxType(t, &numInputs, &numOutputs)) {
		cerr << "ERROR during the evaluation of t : " << boxpp(t) << endl;
		exit(1);
	}
	//cerr << "Documentator : " << numInputs <<" inputs and " << numOutputs <<" outputs for box : " << boxpp(t) << endl;
}


/**
 * Print doc equations, following the Lateq::println method.
 *
 * @param[in]	ltq		The object containing compiled LaTeX code of equations.
 * @param[out]	docout	The output file to print into.
 */
static void printDocEqn(Lateq* ltq, ostream& docout) 
{
	ltq->println(docout);
	//cerr << "Documentator : printDocEqn : "; ltq->println(cerr); cerr << endl;
}


/*****************************************************************************
					Sub-function for <diagram> handling
 *****************************************************************************/

/**
 * @brief Doc diagrams handling.
 * 
 * Three steps :
 * 1. evaluate expression
 * 2. call svg drawing in the appropriate directory
 * 3. print latex figure code with the appropriate directory reference
 *
 * @param[in]	expr		Parsed input expression, as boxes tree.
 * @param[in]	svgTopDir	Basename of the new doc directory ("*-math/svg").
 * @param[out]	docout		The output file to print into.
 */
static void printDocDgm(const Tree expr, const char* svgTopDir, ostream& docout, int i)
{
	/** 1. Evaluate expression. */
	Tree docdgm = evaldocexpr(expr, gExpandedDefList);
	if (gErrorCount > 0) {
		cerr << "Total of " << gErrorCount << " errors during evaluation of : diagram docdgm = " << boxpp(docdgm) << ";\n";
		exit(1);
	}
	
	/**
	 * 2. Draw the diagram after its evaluation, in SVG.
	 * Warning : pdflatex can't directly include SVG files !
	 */
	char dgmid[MAXIDCHARS+1];
	sprintf(dgmid, "%02d", i);
	string thisdgmdir = subst("$0/svg-$1", svgTopDir, dgmid);
	//cerr << "Documentator : printDocDgm : drawSchema in '" << gCurrentDir << "/" << thisdgmdir << "'" << endl;
	
	drawSchema( docdgm, thisdgmdir.c_str(), "svg" );
	
	/** 3. Print LaTeX figure code. */
	char temp[1024];
	const string dgmfilename = legalFileName(docdgm, 1024, temp);
	//docout << "figure \\ref{figure" << i << "}";
	docout << "\\begin{figure}[ht!]" << endl;
	docout << "\t\\centering" << endl;
	docout << "\t\\includegraphics[width=\\textwidth]{" << subst("../svg/svg-$0/", dgmid) << dgmfilename << "}" << endl;
	docout << "\t\\caption{block-diagram of \\texttt{" << dgmfilename << "}}" << endl;
	docout << "\t\\label{figure" << i << "}" << endl;
	docout << "\\end{figure}" << endl << endl;
	
	/** 4. Warn about naming interferences (in the notice). */
	gDocNoticeFlagMap["nameconflicts"] = true;
	gDocNoticeFlagMap["svgdir"] = true;
}



//------------------------ dealing with files -------------------------


/**
 * Create a new directory in the current one.
 */
static int makedir(const char* dirname)
{
	char	buffer[FAUST_PATH_MAX];
	gCurrentDir = getcwd (buffer, FAUST_PATH_MAX);
	
	if ( gCurrentDir.c_str() != 0) {
		int status = mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status == 0 || errno == EEXIST) {
			return 0;
		}
	}
	perror("makedir");
	exit(errno);
}


/**
 * Create a new directory in the current one, 
 * then 'cd' into this new directory.
 * 
 * @remark
 * The current directory is saved to be later restaured.
 */
static int mkchdir(const char* dirname)
{
	char	buffer[FAUST_PATH_MAX];
	gCurrentDir = getcwd (buffer, FAUST_PATH_MAX);

	if ( gCurrentDir.c_str() != 0) {
		int status = mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status == 0 || errno == EEXIST) {
			if (chdir(dirname) == 0) {
				return 0;
			}
		}
	}
	perror("mkchdir");
	exit(errno);
}


/**
 * Switch back to the previously stored current directory
 */
static int cholddir ()
{
	if (chdir(gCurrentDir.c_str()) == 0) {
		return 0;
	} else {
		perror("cholddir");
		exit(errno);
	}
}


/**
 * Get current directory and store it in gCurrentDir.
 */
static void getCurrentDir ()
{
	char	buffer[FAUST_PATH_MAX];
    gCurrentDir = getcwd (buffer, FAUST_PATH_MAX);
}


/**
 * Open architecture file.
 */
static istream* openArchFile (const string& filename)
{
	istream* file;
	getCurrentDir();	// Save the current directory.
	//cerr << "Documentator : openArchFile : Opening input file  '" << filename << "'" << endl;
	if ( (file = open_arch_stream(filename.c_str())) ) {
		//cerr << "Documentator : openArchFile : Opening '" << filename << "'" << endl;
	} else {
		cerr << "ERROR : can't open architecture file " << filename << endl;
		exit(1);
	}
	cholddir();			// Return to current directory.
	return file;
}


/**
 * Transform the definition name property of tree <t> into a
 * legal file name.  The resulting file name is stored in
 * <dst> a table of at least <n> chars. Returns the <dst> pointer
 * for convenience.
 */
static char* legalFileName(const Tree t, int n, char* dst)
{
	Tree	id;
	int 	i=0;
	if (getDefNameProperty(t, id)) {
		const char* 	src = tree2str(id);
		for (i=0; isalnum(src[i]) && i<16; i++) {
			dst[i] = src[i];
		}
	}
	dst[i] = 0;
	if (strcmp(dst, "process") != 0) { 
		// if it is not process add the hex address to make the name unique
		snprintf(&dst[i], n-i, "-%p", t);
	}
	return dst;
}

/**
 * Simply concat a string with a number in a "%03d" format.
 * The number has MAXIDCHARS characters. 
 **/
static string calcNumberedName(const char* base, int i)
{
	char nb[MAXIDCHARS+1];
	sprintf(nb, "%03d", i);
	return subst("$0$1", base, nb);
}

/**
 * Remove the leading and trailing double quotes of a string
 * (but not those in the middle of the string)
 */
static string rmExternalDoubleQuotes(const string& s)
{
    size_t i = s.find_first_not_of("\"");
    size_t j = s.find_last_not_of("\"");
	
    if ( (i != string::npos) & (j != string::npos) ) {
        return s.substr(i, 1+j-i);
    } else {
        return "";
    }
}


/** 
 * Copy all Faust source files into an 'src' subdirectory.
 *
 * @param[in]	projname		Basename of the new doc directory ("*-math").
 * @param[in]	pathnames		The paths list of the source files to copy.
 */
static void copyFaustSources(const char* projname, const vector<string>& pathnames)
{
	string srcdir = subst("$0/src", projname);
	//cerr << "Documentator : copyFaustSources : Creating directory '" << srcdir << "'" << endl;
	makedir(srcdir.c_str());	// create a directory.
		
	for (unsigned int i=0; i< pathnames.size(); i++) {
		ifstream src;
		ofstream dst;
		string faustfile = pathnames[i];
		string copy = subst("$0/$1", srcdir, filebasename(faustfile.c_str()));
		//cerr << "Documentator : copyFaustSources : Opening input file  '" << faustfile << "'" << endl;
		//cerr << "Documentator : copyFaustSources : Opening output file '" << copy << "'" << endl;
		src.open(faustfile.c_str(), ifstream::in);
		dst.open(copy.c_str(), ofstream::out);
		string	s;
		while ( getline(src,s) ) dst << s << endl;
	}
}


//------------------------ date managment -------------------------


static void initCompilationDate()
{
	time_t now;
	
	time(&now);
	gCompilationDate = *localtime(&now);
}

static struct tm* getCompilationDate()
{
	initCompilationDate();
	return &gCompilationDate;
}
