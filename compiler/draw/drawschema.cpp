/************************************************************************
 ************************************************************************
    FAUST compiler
	Copyright (C) 2003-2004 GRAME, Centre National de Creation Musicale
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

 /**
 * @file drawschema.cpp
 * Implement block-diagram schema generation in svg or postscript format.
 * The result is a folder containing one or more schema files in svg or
 * ps format. Complex block-diagrams are automatically splitted.
 */


#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

#include <ostream>
#include <sstream>
#include <set>
#include <utility>
#include <map>
#include <stack>
#include <string>

#include "boxes.hh"
#include "ppbox.hh"
#include "prim2.hh"

#include <vector>
#include "devLib.h"
#include "ppbox.hh"
#include "xtended.hh"
#include "eval.hh"
#include "occurrences.hh"
#include "boxcomplexity.h"

#include "schema.h"
#include "drawschema.hh"

#define linkcolor "#b3d1dc"
#define normalcolor "#ffeaa2"
#define uicolor "#F1CFA1"
#define slotcolor "#ffffd7"
#define numcolor "#ffffff"

using namespace std;

// external parameters
extern int gFoldThreshold;				// max diagram complexity before folding


// internal state during drawing
static Occurrences* 	gOccurrences;
static bool				sFoldingFlag;		// true with complex block-diagrams
static stack<Tree>		gPendingExp;		// Expressions that need to be drawn
static set<Tree>		gDrawnExp;			// Expressions drawn or scheduled so far
static const char* 		gDevSuffix;			// .svg or .ps used to choose output device
static char 			gCurrentDir[512];	// room to save current directory name
static string			gSchemaFileName;	// name of schema file beeing generated
static map<Tree,string>	gBackLink;			// link to enclosing file for sub schema

// prototypes of internal functions
static void 	writeSchemaFile(Tree bd);
static schema* 	generateDiagramSchema (Tree bd);
static schema* 	generateInsideSchema(Tree t);
static void 	scheduleDrawing(Tree t);
static bool 	pendingDrawing(Tree& t);
static schema* 	generateAbstractionSchema(schema* x, Tree t);
static schema* 	generateOutputSlotSchema(Tree a);
static schema* 	generateInputSlotSchema(Tree a);
static schema* 	generateBargraphSchema(Tree t);
static schema* 	generateUserInterfaceSchema(Tree t);
static char* 	legalFileName(Tree t, int n, char* dst);
static int 		cholddir ();
static int 		mkchdir(const char* dirname);




/**
 *The entry point to generate from a block diagram as a set of
 *svg files stored in the directory "<projname>-svg/" or
 *"<projname>-ps/" depending of <dev>.
 */
void drawSchema(Tree bd, const char* projname, const char* dev)
{
	gDevSuffix 		= dev;
	sFoldingFlag 	= boxComplexity(bd) > gFoldThreshold;

	mkchdir(projname); 			// create a directory to store files

	scheduleDrawing(bd);		// schedule the initial drawing

	Tree t; while (pendingDrawing(t)) {
		writeSchemaFile(t);		// generate all the pending drawing
	}

	cholddir();					// return to current directory
}


/************************************************************************
 ************************************************************************
							IMPLEMENTATION
 ************************************************************************
 ************************************************************************/


//------------------- to schedule and retreive drawing ------------------

/**
 * Schedule a makeBlockSchema diagram to be drawn.
 */
static void scheduleDrawing(Tree t)
{
	if (gDrawnExp.find(t) == gDrawnExp.end()) {
		gDrawnExp.insert(t);
		gBackLink.insert(make_pair(t,gSchemaFileName));	// remember the enclosing filename
		gPendingExp.push(t);
	}
}

/**
 * Retrieve next block diagram that must be drawn
 */
static bool pendingDrawing(Tree& t)
{
	if (gPendingExp.empty()) return false;
	t = gPendingExp.top();
	gPendingExp.pop();
	return true;
}



//------------------------ dealing with files -------------------------

/**
 * Write a top level diagram. A top level diagram
 * is decorated with is definition name property
 * and is drawn in an individual file
 */
static void writeSchemaFile(Tree bd)
{
	Tree			id;
	schema* 		ts;

	char 			temp[1024];

	gOccurrences = new Occurrences(bd);

	bool hasname = getDefNameProperty(bd, id); assert(hasname);

	// generate legal file name for the schema
	stringstream s1; s1 << legalFileName(bd, 1024, temp) << "." << gDevSuffix;
	gSchemaFileName = s1.str();

	// generate the label of the schema
	stringstream s2; s2 << tree2str(id);
	string link = gBackLink[bd];
	ts = makeTopSchema(generateInsideSchema(bd), 20, s2.str(), link);
	// draw to the device defined by gDevSuffix
	if (strcmp(gDevSuffix, "svg") == 0) {
		SVGDev dev(s1.str().c_str(), ts->width(), ts->height());
		ts->place(0,0, kLeftRight);
		ts->draw(dev);
	} else {
		PSDev dev(s1.str().c_str(), ts->width(), ts->height());
		ts->place(0,0, kLeftRight);
		ts->draw(dev);
	}
}



/**
 * Create a new directory in the current one to store the diagrams.
 * The current directory is saved to be later restaured.
 */
static int mkchdir(const char* dirname)
{
	//cerr << "mkchdir of " << dirname << endl;
	if (getcwd(gCurrentDir, 512) != 0) {
		int status = mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (status == 0 || errno == EEXIST) {
			if (chdir(dirname) == 0) {
				return 0;
			}
		}
	}
	perror("mkchdir");
	exit(errno);
	//return errno;
}


/**
 *Switch back to the previously stored current directory
 */
static int cholddir ()
{
	if (chdir(gCurrentDir) == 0) {
		return 0;
	} else {
		perror("cholddir");
		exit(errno);
	}
}


/**
 * Transform the definition name property of tree <t> into a
 * legal file name.  The resulting file name is stored in
 * <dst> a table of at least <n> chars. Returns the <dst> pointer
 * for convenience.
 */
static char* legalFileName(Tree t, int n, char* dst)
{
	Tree	id;
	int 	i=0;
	if (getDefNameProperty(t, id)) {
		const char* 	src = tree2str(id);
		for (i=0; isalnum(src[i]) && i<16; i++) {
			dst[i] = src[i];
		}
	}
	snprintf(&dst[i], n-1, "-%p", t);
	return dst;
}



//------------------------ generating the schema -------------------------

/**
 * Generate an appropriate schema according to
 * the type of block diagram. When folding is requiered,
 * instead of going down block-diagrams with a name,
 * schedule them for an individual file.
 */
static schema* generateDiagramSchema(Tree t)
{
	Tree	id;
	int		ins, outs;

	//cerr << t << " generateDiagramSchema " << boxpp(t)<< endl;

	if (getDefNameProperty(t, id)) {
		stringstream 	s; s << tree2str(id);
		//cerr << t << "\tNAMED : " << s.str() << endl;
	}

	if ( sFoldingFlag && /*(gOccurrences->getCount(t) > 0) &&*/
			(boxComplexity(t) > 2) && getDefNameProperty(t, id)) {
		char 	temp[1024];
		getBoxType(t, &ins, &outs);
		stringstream s, l;
		s << tree2str(id);
		l << legalFileName(t,1024,temp) << "." << gDevSuffix;
		scheduleDrawing(t);
		return makeBlockSchema(ins, outs, s.str(), linkcolor, l.str());

	} else  if (getDefNameProperty(t, id) && ! isBoxSlot(t)) {
		// named case : not a slot, with a name
		// draw a line around the object with its name
		stringstream 	s; s << tree2str(id);
		return makeDecorateSchema(generateInsideSchema(t), 10, s.str());

	} else {
		// normal case
		return generateInsideSchema(t);
	}
}



/**
 * Generate the inside schema of a block diagram
 * according to its type
 */
static schema* generateInsideSchema(Tree t)
{
	Tree a, b, ff, l, type,name,file;
	int		i;
	float	r;
	prim0	p0;
	prim1	p1;
	prim2	p2;
	prim3	p3;
	prim4	p4;
	prim5	p5;


	xtended* xt = (xtended*)getUserData(t);

	if (xt)							{ return makeBlockSchema(xt->arity(), 1, xt->name(), normalcolor, ""); }

	else if (isBoxInt(t, &i))		{ stringstream 	s; s << i; return makeBlockSchema(0, 1, s.str(), numcolor, "" ); }
	else if (isBoxReal(t, &r)) 		{ stringstream 	s; s << r; return makeBlockSchema(0, 1, s.str(), numcolor, "" ); }
	else if (isBoxWire(t)) 			{ return makeCableSchema(); }
	else if (isBoxCut(t)) 			{ return makeCutSchema();  }

	else if (isBoxPrim0(t, &p0)) 	{ return makeBlockSchema(0, 1, prim0name(p0), normalcolor, ""); }
	else if (isBoxPrim1(t, &p1)) 	{ return makeBlockSchema(1, 1, prim1name(p1), normalcolor, ""); }
	else if (isBoxPrim2(t, &p2)) 	{ return makeBlockSchema(2, 1, prim2name(p2), normalcolor, ""); }
	else if (isBoxPrim3(t, &p3)) 	{ return makeBlockSchema(3, 1, prim3name(p3), normalcolor, ""); }
	else if (isBoxPrim4(t, &p4)) 	{ return makeBlockSchema(4, 1, prim4name(p4), normalcolor, ""); }
	else if (isBoxPrim5(t, &p5)) 	{ return makeBlockSchema(5, 1, prim5name(p5), normalcolor, ""); }

	else if (isBoxFFun(t, ff)) 					{ return makeBlockSchema(ffarity(ff), 1, ffname(ff), normalcolor, ""); }
	else if (isBoxFConst(t, type,name,file)) 	{ return makeBlockSchema(0, 1, tree2str(name), normalcolor, ""); }

	else if (isBoxButton(t)) 		{ return generateUserInterfaceSchema(t); }
	else if (isBoxCheckbox(t)) 		{ return generateUserInterfaceSchema(t); }
	else if (isBoxVSlider(t)) 		{ return generateUserInterfaceSchema(t); }
	else if (isBoxHSlider(t)) 		{ return generateUserInterfaceSchema(t); }
	else if (isBoxNumEntry(t)) 		{ return generateUserInterfaceSchema(t); }
	else if (isBoxVBargraph(t))		{ return generateBargraphSchema(t); }
	else if (isBoxHBargraph(t))		{ return generateBargraphSchema(t); }

	// don't draw group rectangle when labels are empty (ie "")
	else if (isBoxVGroup(t,l,a))	{ 	stringstream s; s << "vgroup(" << tree2str(l) << ")";
										schema* r = generateDiagramSchema(a);
									  	return makeDecorateSchema(r, 10, s.str()); }
	else if (isBoxHGroup(t,l,a))	{ 	stringstream s; s << "hgroup(" << tree2str(l) << ")";
										schema* r = generateDiagramSchema(a);
									  	return makeDecorateSchema(r, 10, s.str()); }
	else if (isBoxTGroup(t,l,a))	{ 	stringstream s; s << "tgroup(" << tree2str(l) << ")";
										schema* r = generateDiagramSchema(a);
									  	return makeDecorateSchema(r, 10, s.str()); }

	else if (isBoxSeq(t, a, b)) 	{ return makeSeqSchema(generateDiagramSchema(a), generateDiagramSchema(b)); }
	else if (isBoxPar(t, a, b)) 	{ return makeParSchema(generateDiagramSchema(a), generateDiagramSchema(b)); }
	else if (isBoxSplit(t, a, b)) 	{ return makeSplitSchema(generateDiagramSchema(a), generateDiagramSchema(b)); }
	else if (isBoxMerge(t, a, b)) 	{ return makeMergeSchema(generateDiagramSchema(a), generateDiagramSchema(b)); }
	else if (isBoxRec(t, a, b)) 	{ return makeRecSchema(generateDiagramSchema(a), generateDiagramSchema(b)); }

	else if (isBoxSlot(t, &i))		{ return generateOutputSlotSchema(t); }
	else if (isBoxSymbolic(t,a,b))	{
		Tree 	id;
		if (getDefNameProperty(t, id)) {
			return generateAbstractionSchema(generateInputSlotSchema(a), b);
		} else {
			return makeDecorateSchema(generateAbstractionSchema(generateInputSlotSchema(a), b), 10, "Abstraction");
		}
	}

	else {

		fprintf(stderr, "Internal Error, box expression not recognized : "); print(t, stderr); fprintf(stderr, "\n");
		exit(1);

	}
}

//------------------------ specific schema -------------------------


/**
 * Generate a 0->1 block schema for a user interface element
 */
static schema* 	generateUserInterfaceSchema(Tree t)
{
	stringstream 	s;
	s << boxpp(t);

	return makeBlockSchema(0, 1, s.str(), uicolor, "");
}



/**
 * Generate a 1->1 block schema for a user interface bargraph
 */
static schema* generateBargraphSchema(Tree t)
{
	stringstream 	s;
	s << boxpp(t);

	return makeBlockSchema(1, 1, s.str(), uicolor, "");
}



/**
 * Generate a 1->0 block schema for an input slot
 */
static schema* generateInputSlotSchema(Tree a)
{
	Tree id; assert(getDefNameProperty(a, id));
	stringstream s; s << tree2str(id);
	return makeBlockSchema(1, 0, s.str(), slotcolor, "");
}



/**
 * Generate a 0->1 block schema for an output slot
 */
static schema* generateOutputSlotSchema(Tree a)
{
	Tree id; assert(getDefNameProperty(a, id));
	stringstream s; s << tree2str(id);
	return makeBlockSchema(0, 1, s.str(), slotcolor, "");
}



/**
 * Generate an abstraction schema by placing in sequence
 * the input slots and the body
 */
static schema* generateAbstractionSchema(schema* x, Tree t)
{
	Tree 	a,b;

	while (isBoxSymbolic(t,a,b)) {
		x = makeParSchema(x, generateInputSlotSchema(a));
		t = b;
	}
	return makeSeqSchema(x, generateDiagramSchema(t));
}
