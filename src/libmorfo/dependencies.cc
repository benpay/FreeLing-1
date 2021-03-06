//////////////////////////////////////////////////////////////////
//
//    FreeLing - Open Source Language Analyzers
//
//    Copyright (C) 2004   TALP Research Center
//                         Universitat Politecnica de Catalunya
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU General Public
//    License as published by the Free Software Foundation; either
//    version 2.1 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    General Public License for more details.
//
//    You should have received a copy of the GNU General Public
//    License along with this library; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//
//    contact: Lluis Padro (padro@lsi.upc.es)
//             TALP Research Center
//             despatx C6.212 - Campus Nord UPC
//             08034 Barcelona.  SPAIN
//
////////////////////////////////////////////////////////////////


#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>

#include "regexp-pcre++.h"
#include "freeling/traces.h"
#include "freeling/dep_rules.h"
#include "freeling/dependencies.h"

using namespace std;

#define MOD_TRACENAME "DEP_TXALA"
#define MOD_TRACECODE DEP_TRACE


set<string> check_wordclass::wordclasses; 


//---------- Class completer ----------------------------------

///////////////////////////////////////////////////////////////
/// Constructor. Load a tree-completion grammar 
///////////////////////////////////////////////////////////////

completer::completer(const string &filename) {

  RegEx blankline("^[ \t]*$");
  int lnum=0;
  string path=filename.substr(0,filename.find_last_of("/\\")+1);

  ifstream fin;
  fin.open(filename.c_str());  
  if (fin.fail()) ERROR_CRASH("Cannot open completer rules file "+filename);

  string line;
  check_wordclass::wordclasses.clear();

  active_flags.insert("INIT");

  int reading=0; 
  while (getline(fin,line)) {
    lnum++;
    

    if (blankline.Search(line) || line[0]=='%') {} // ignore blank/empty lines, and comments

    else if (line == "<CLASS>") reading=1;
    else if (line == "</CLASS>") reading=0;

    else if (line == "<GRPAR>") reading=2;
    else if (line == "</GRPAR>") reading=0;

    else if (reading==1) {
      // load CLASS section
      string vclass, vlemma;
      istringstream sin;  sin.str(line);
      sin>>vclass>>vlemma;
      
      if (vlemma[0]=='"' && vlemma[vlemma.size()-1]=='"') {
	// Class defined in a file, load it.
	string fname= util::absolute(vlemma,path);  // if relative, use main file path as base
	
	ifstream fclas;
	fclas.open(fname.c_str());  	
	if (fclas.fail()) ERROR_CRASH("Cannot open word class file "+fname);
	
	while (getline(fclas,line)) {
	  if(!line.empty() && line[0]!='%') {
	    istringstream sline;
	    sline.str(line);
	    sline>>vlemma;
	    check_wordclass::wordclasses.insert(vclass+"#"+vlemma);
	  }
	}
	fclas.close();
      }
      else {
	// Enumerated class, just add the word
	check_wordclass::wordclasses.insert(vclass+"#"+vlemma);
      }       
    }
    else if (reading==2) {
      ////// load GRPAR section defining tree-completion rules
      completerRule r;
      r.line=lnum;

      istringstream sline; sline.str(line);
      string flags,chunks,lit,newlabels;
      sline>>r.weight>>flags>>r.context>>chunks>>r.operation>>lit>>newlabels;
      r.enabling_flags=util::string2set(flags,"|");

      string::size_type p=newlabels.find(":");
      if (lit=="RELABEL" && p!=string::npos) {
	r.newNode1 = newlabels.substr(0,p);
	r.newNode2 = newlabels.substr(p+1);
      }
      else {
        r.newNode1 = newlabels;
        r.newNode2 = "-";
      }

      // read flags to activate/deactivate until line ends or a comment is found
      bool comm=false;
      while (!comm && sline>>flags) {
        if (flags[0]=='%') comm=true;
        else if (flags[0]=='+') r.flags_toggle_on.insert(flags.substr(1));
        else if (flags[0]=='-') r.flags_toggle_off.insert(flags.substr(1));
	else WARNING("Syntax error reading completer rule at line "+util::int2string(lnum)+". Flag must be toggled either on (+) or off (-)");
      }

      if ((r.operation=="top_left" || r.operation=="top_right") && lit!="RELABEL")
	WARNING("Syntax error reading completer rule at line "+util::int2string(lnum)+". "+r.operation+" requires RELABEL.");
      if ((r.operation=="last_left" || r.operation=="last_right" || r.operation=="cover_last_left") && lit!="MATCHING")	
	WARNING("Syntax error reading completer rule at line "+util::int2string(lnum)+". "+r.operation+" requires MATCHING.");

      if (r.context=="-") r.context="$$";
      r.context_neg=false;
      if (r.context[0]=='!') {
	r.context_neg=true;
	r.context=r.context.substr(1);
      }

      p=chunks.find(",");
      if (chunks[0]!='(' || chunks[chunks.size()-1]!=')' || p==string::npos)  
	WARNING("Syntax error reading completer rule at line "+util::int2string(lnum)+". Expected (leftChunk,rightChunk) pair at: "+chunks);
      
      r.leftChk=chunks.substr(1,p-1);
      r.rightChk=chunks.substr(p+1,chunks.size()-p-2);

      // check if the chunk labels carried extra lemma/form/class/parole
      // conditions and separate them if that's the case.
      extract_conds(r.leftChk,r.leftConds,r.leftRE);
      extract_conds(r.rightChk,r.rightConds,r.rightRE);

      TRACE(4,"Loaded rule: [line "+util::int2string(r.line)+"] "+util::int2string(r.weight)+" "+util::set2string(r.enabling_flags,"|")+" "+(r.context_neg?"not:":"")+r.context+" ("+r.leftChk+util::list2string(r.leftConds,"")+","+r.rightChk+util::list2string(r.rightConds,"")+") "+r.operation+" "+r.newNode1+":"+r.newNode2+" +("+util::set2string(r.flags_toggle_on,"/")+") -("+util::set2string(r.flags_toggle_off,"/")+")");

      chgram[make_pair(r.leftChk,r.rightChk)].push_back(r);
    }
  }

  fin.close();

  TRACE(1,"tree completer successfully created");
}


//-- tracing ----------------------
void PrintTree(parse_tree::iterator n, int depth) {
  parse_tree::sibling_iterator d;
  
  cout<<string(depth*2,' '); 
  if (n->num_children()==0) { 
    if (n->info.is_head()) { cout<<"+";}
    word w=n->info.get_word();
    cout<<"("<<w.get_form()<<" "<<w.get_lemma()<<" "<<w.get_parole();
    cout<<")"<<endl;
  }
  else { 
    if (n->info.is_head()) { cout<<"+";}
    cout<<n->info.get_label()<<"_[ "<<endl;
    for (d=n->sibling_begin(); d!=n->sibling_end(); ++d)
      PrintTree(d, depth+1);
    cout<<string(depth*2,' ')<<"]"<<endl;
  }
}
//--
//-- tracing ----------------------
void PrintDepTree(dep_tree::iterator n, int depth) {
  dep_tree::sibling_iterator d;
  
  cout<<string(depth*2,' '); 
  if (n->num_children()==0) { 
    if (n->info.is_head()) { cout<<"+";}
    word w=n->info.get_word();
    cout<<"("<<w.get_form()<<" "<<w.get_lemma()<<" "<<w.get_parole();
    cout<<")"<<endl;
  }
  else { 
    if (n->info.is_head()) { cout<<"+";}
    cout<<n->info.get_label()<<"_[ "<<endl;
    for (d=n->sibling_begin(); d!=n->sibling_end(); ++d)
      PrintDepTree(d, depth+1);
    cout<<string(depth*2,' ')<<"]"<<endl;
  }
}
//--


///////////////////////////////////////////////////////////////
/// Complete a partial parse tree.
///////////////////////////////////////////////////////////////

parse_tree completer::complete(parse_tree &tr, const string & startSymbol) {

  TRACE(3,"---- COMPLETING chunking to get a full parse tree ----");

  // we only complete trees with the fake start symbol set by the chart_parser
  if (tr.begin()->info.get_label() != startSymbol) return tr;  

  int maxchunk = tr.num_children();
  int nchunk=1;
  
  vector<parse_tree *> trees;
  
  for(parse_tree::sibling_iterator ichunk=tr.sibling_begin(); ichunk!=tr.sibling_end(); ++ichunk,++nchunk) {
    TRACE(4,"Creating empty tree");

    parse_tree * mtree = new parse_tree(ichunk);
    if (ichunk->num_children()==0) {
      // the chunk is a leaf. Create a non-terminal node to ease 
      // the job in case no rules are found
      node nod(ichunk->info.get_label());
      nod.set_chunk(true);
      nod.set_head(false);
      parse_tree *taux=new parse_tree(nod);
      // hang the original node under the new one.
      mtree->info.set_head(true);
      taux->hang_child(*mtree);
      // use the new tree
      mtree=taux;
    }

    mtree->info.set_chunk(nchunk);
    trees.push_back(mtree);
    TRACE(4,"    Done");
  }
  
  // Apply as many rules as number of chunks minus one (since each rule fusions two chunks in one)
  for (nchunk=0; nchunk<maxchunk-1; ++nchunk) {
    
    if (traces::TraceLevel>=2) {
      TRACE(2,"");
      TRACE(2,"REMAINING CHUNKS:");
      vector<parse_tree *>::iterator vch; 
      int p=0;
      for (vch=trees.begin(); vch!=trees.end(); vch++) {
	TRACE(2,"    chunk.num="+util::int2string((*vch)->info.get_chunk_ord())+"  (trees["+util::int2string(p)+"]) \t"+(*vch)->info.get_label());
	p++;
      }
    }

    TRACE(3,"LOOKING FOR BEST APPLICABLE RULE");
    size_t chk=0;
    completerRule bestR = find_grammar_rule(trees, chk);
    int best_prio= bestR.weight;
    size_t best_pchunk = chk;

    chk=1;
    while (chk<trees.size()-1) {
      completerRule r = find_grammar_rule(trees, chk);

      if ( (r.weight==best_prio && chk<best_pchunk) || (r.weight>0 && r.weight<best_prio) || (best_prio<=0 && r.weight>best_prio) ) {
	best_prio = r.weight;
        best_pchunk = chk;
	bestR = r;
      }
      
      chk++;
    }
    
    TRACE(2,"BEST RULE SELECTED. Apply rule [line "+util::int2string(bestR.line)+"] "+util::int2string(bestR.weight)+" "+util::set2string(bestR.enabling_flags,"|")+" "+(bestR.context_neg?"not:":"")+bestR.context+" ("+bestR.leftChk+util::list2string(bestR.leftConds,"")+","+bestR.rightChk+util::list2string(bestR.rightConds,"")+") "+bestR.operation+" "+bestR.newNode1+":"+bestR.newNode2+" +("+util::set2string(bestR.flags_toggle_on,"/")+") -("+util::set2string(bestR.flags_toggle_off,"/")+")  to chunk trees["+util::int2string(best_pchunk)+"]");
    
    parse_tree * resultingTree=applyRule(bestR, trees[best_pchunk], trees[best_pchunk+1]);

    TRACE(2,"Rule applied - Erasing chunk in trees["+util::int2string(best_pchunk+1)+"]");
    trees[best_pchunk]=resultingTree;
    trees[best_pchunk+1]=NULL;     
    vector<parse_tree*>::iterator end = remove (trees.begin(), trees.end(), (parse_tree*)NULL);    
    trees.erase (end, trees.end());    
  }
  
  // rebuild node index with new iterators, maintaining id's
  parse_tree ret_val = (*trees[0]); 
  delete trees[0];
  ret_val.rebuild_node_index();
  return (ret_val);
}

//---------- completer private functions 




///////////////////////////////////////////////////////////////
/// Separate extra lemma/form/class conditions from the chunk label
///////////////////////////////////////////////////////////////

#define closing(x) (x=='('?")":(x=='<'?">":(x=='{'?"}":(x=='['?"]":""))))

void completer::extract_conds(string &chunk, list<string> &conds, RegEx &re) const {

  string seen="";
  string con="";
  conds.clear();

  // If the label has pairs of "<>" "()" or "[]", split them apart
  if ( chunk.find_first_of("<")!=string::npos 
       || chunk.find_first_of("(")!=string::npos 
       || chunk.find_first_of("[")!=string::npos
       || chunk.find_first_of("{")!=string::npos ) {

    // locate start of first pair, and separate chunk label
    size_t p=chunk.find_first_of("<([{");
    con = chunk.substr(p);
    chunk = chunk.substr(0,p);

    // process pairs
    p=0;
    while (p!=string::npos) {

      string close=closing(con[p]);
      size_t q=con.find_first_of(close);
      if (q==string::npos) {
	WARNING("Missing closing "+close+" in dependency rule. All conditions ignored.");
	conds.clear();
	return;
      }
      
      // add the condition to the list
      conds.push_back(con.substr(p,q-p+1));
      // keep regexp (without brackets) if the condition is on PoS
      if (con[p]=='{') re = RegEx(con.substr(p+1,q-p-1));

      // check for duplicates
      if (seen.find(con[p])!=string::npos) {
	WARNING("Duplicate bracket pair "+con.substr(p,1)+close+" in dependency rule. All conditions ignored.");
	conds.clear();
	return;
      }
      seen = seen + con[p];

      // find start of next pair, if any
      p=con.find_first_of("<([{",q);
    }
  }

}


///////////////////////////////////////////////////////////////
/// Check if the current context matches the one specified 
/// in the given rule.
///////////////////////////////////////////////////////////////

#define LEFT 1
#define RIGHT 2
#define first_cond(d) (d==LEFT? core-1 : core+1)
#define first_chk(d)  (d==LEFT? chk-1  : chk+2)
#define last(i,v,d)   (d==LEFT? i<0    : i>=(int)v.size())
#define next(i,d)     (d==LEFT? i-1    : i+1);
#define prev(i,d)     (d==LEFT? i+1    : i-1);

bool completer::match_side(const int dir, const vector<parse_tree *> &trees, const size_t chk, const vector<string> &conds, const size_t core) const {

  TRACE(4,"        matching "+(dir==LEFT?string("LEFT"):string("RIGHT"))+" context. core position="+util::int2string(core)+"   chunk position="+util::int2string(chk));
  
  // check whether left/right context matches
  bool match=true;
  int j=first_cond(dir); // condition index
  int k=first_chk(dir);  // chunk index
  while ( !last(j,conds,dir) && match ) {
    TRACE(4,"        condition.idx j="+util::int2string(j)+"   chunk.idx k="+util::int2string(k));

    if (last(k,trees,dir) && conds[j]!="OUT") {
      // fail if context is shorter than rule (and the condition is not OUT-of-Bounds)
      match=false; 
    }
    else if (!last(k,trees,dir)) {
      bool b = (conds[j]!="*") && (conds[j]=="?" || match_pattern(trees[k],conds[j]));
      TRACE(4,"        conds[j]="+conds[j]+" trees[k]="+trees[k]->begin()->info.get_label());

      if ( !b && conds[j]=="*" ) {
        // let the "*" match any number of items, looking for the next condition
        TRACE(4,"        matching * wildcard.");

	j=next(j,dir);
        if (conds[j]=="OUT") // OUT after a wildcard will always match
          b=true;
        else {
	  while ( !last(k,trees,dir) && !b ) {
	    b = (conds[j]=="?") || match_pattern(trees[k],conds[j]);
	    TRACE(4,"           j="+util::int2string(j)+"  k="+util::int2string(k));
	    TRACE(4,"           conds[j]="+conds[j]+" trees[k]="+trees[k]->begin()->info.get_label()+"   "+(b?string("matched"):string("no match")));
	    k=next(k,dir);
	  }
	  
	  k=prev(k,dir);	
	}
      }

      match = b;
    }

    k=next(k,dir);
    j=next(j,dir);
  }
  
  return match;  
}


///////////////////////////////////////////////////////////////
/// Check if the current context matches the one specified 
/// in the given rule.
///////////////////////////////////////////////////////////////

bool completer::matching_context(const vector<parse_tree *> &trees, const size_t chk, const completerRule &r) const {

  // load current rule pattern in a vector, and locate the place matching the current chunks
  vector<string> conds = util::string2vector(r.context,"_");
  size_t core;
  for (core=0; core<conds.size() && conds[core]!="$$"; core++);
  
  TRACE(4,"        core position="+util::int2string(core)+"   chunk position="+util::int2string(chk));
  
  // check whether the context matches      
  bool match = match_side(LEFT, trees, chk, conds, core) && match_side(RIGHT, trees, chk, conds, core);

  // apply negation if necessary
  if (r.context_neg) match = !match;

  TRACE(4,"        Context "+string(match?"matches":"does NOT match"));
  return match;
}


///////////////////////////////////////////////////////////////
/// check if the extra lemma/form/class conditions are satisfied.
///////////////////////////////////////////////////////////////

bool completer::matching_condition(parse_tree::iterator chunk, const list<string> &conds, RegEx &rx) const {

  // we know that the labels match, just check the extra conditions

  // if no conditions, we're done
  if (conds.empty()) return true;

  // dive in the tree down to the head word.
  parse_tree::iterator head = chunk->begin();
  // while we don't reach a leaf
  while (head->num_children()>0) {
    // locate the head children..
    parse_tree::sibling_iterator s;
    for (s=head->sibling_begin(); s!=head->sibling_end() && !s->info.is_head(); ++s);    
    if (s==head->sibling_end())
      WARNING("NO HEAD Found!!! Check your chunking grammar and your dependency-building rules.");

    // ...and get down to it
    head=s;
  }

  // the head leaf is located, get the word.
  word w=head->info.get_word();

  // check if all the conditions are satisfied
  bool ok=true;
  for (list<string>::const_iterator c=conds.begin(); ok && c!=conds.end(); c++) {
    TRACE(4,"        matching condition "+(*c)+" with word ("+w.get_form()+","+w.get_lemma()+","+w.get_parole()+")");
    switch ((*c)[0]) {
      case '<':	ok = ("<"+w.get_lemma()+">" == (*c));   break;	
      case '(':	ok = ("("+w.get_form()+")" == (*c));    break;
      case '{':	ok = rx.Search(w.get_parole());	break;
      case '[': {
	string vclass=c->substr(1,c->size()-2);
	ok = (check_wordclass::wordclasses.find(vclass+"#"+w.get_lemma()) != check_wordclass::wordclasses.end());	
	TRACE(4,"        CLASS: "+(*c)+string(ok?" matches lemma":" does NOT match lemma"));
	break;
      }	
      default: break; // should never get this far
    }
  }

  TRACE(4,"        Conditions "+string(ok?"match":"do NOT match"));
  return ok;
}


///////////////////////////////////////////////////////////////
/// check if a pattern of the type label<lemma>, label(form),
/// etc., matches a given node in the tree.
///////////////////////////////////////////////////////////////

bool completer::match_pattern(parse_tree::iterator chunk, const string &pattern) const {

  // detect whether condition is negated
  bool neg = (pattern[0]=='~');
  string patr = (neg? pattern.substr(1) : pattern);
  
  // separate alternative patterns, if any
  vector<string> alts=util::string2vector(patr,"|"); 

  // look for a matching alt
  bool found=false;
  for (size_t i=0; i<alts.size() && !found; i++) {
    // separate label and conditions, if any
    list<string> conds;  RegEx re("");
    extract_conds(alts[i],conds,re);
    
    // check if the label matches, and if so, the conditions
    found = (alts[i]==chunk->info.get_label()) && matching_condition(chunk, conds, re);
  }

  if (neg) found= !found;

  TRACE(5,"           Pattern "+string(found?"found":"NOT found"));
  return found;
}


///////////////////////////////////////////////////////////////
/// check if the operation is executable (for last_left/last_right cases)
///////////////////////////////////////////////////////////////

bool completer::matching_operation(const vector<parse_tree *> &trees, const size_t chk, completerRule &r) const {

  // "top" operations are always feasible
  if (r.operation != "last_left" && r.operation!="last_right" && r.operation!="cover_last_left") 
    return true;

  // "last_X" operations, require checking for the condition node
  size_t t=0;
  if (r.operation=="last_left" || r.operation=="cover_last_left") t=chk;
  else if (r.operation=="last_right") t=chk+1;

  // locate last_left/right matching node 
  r.last=NULL;
  parse_tree::iterator i;
  for (i=trees[t]->begin(); i!=trees[t]->end(); ++i) {
    TRACE(5,"           matching operation: "+r.operation+". Rule expects "+r.newNode1+", node is "+i->info.get_label());
    if (match_pattern(&(*i),r.newNode1)) 
      r.last=&(*i);  // remember node location in case the rule is finally selected.
  }

  TRACE(4,"        Operation "+string(r.last!=NULL?"matches":"does NOT match"));
  return r.last!=NULL;
}


///////////////////////////////////////////////////////////////
/// Find out if currently active flags enable the given rule
///////////////////////////////////////////////////////////////

bool completer::enabled_rule(const completerRule &r) const {
   
  // if the rule is always-enabled, ignore everthing else
  if (r.enabling_flags.find("-") != r.enabling_flags.end()) 
    return true;

  // look through the rule enabling flags, to see if any is active.
  bool found=false;
  set<string>::const_iterator x;
  for (x=r.enabling_flags.begin(); !found && x!=r.enabling_flags.end(); x++)
    found = (active_flags.find(*x)!=active_flags.end());
  
  return found;
}

///////////////////////////////////////////////////////////////
/// Look for a completer grammar rule matching the given
/// chunk in "chk" position of "trees" and his right-hand-side mate.
///////////////////////////////////////////////////////////////

completerRule completer::find_grammar_rule(const vector<parse_tree *> &trees, const size_t chk) {

  string leftChunk = trees[chk]->begin()->info.get_label();
  string rightChunk = trees[chk+1]->begin()->info.get_label();
  TRACE(3,"  Look up rule for: ("+leftChunk+","+rightChunk+")");

  // find rules matching the chunks
  map<pair<string,string>,list<completerRule> >::iterator r;
  r = chgram.find(make_pair(leftChunk,rightChunk));
 
  list<completerRule>::iterator i;  
  list<completerRule>::iterator best;
  int bprio= -1;
  bool found=false;
  if (r != chgram.end()) { 

    // search list of candidate rules for any rule matching conditions, context, and flags
    for (i=r->second.begin(); i!=r->second.end(); i++) {

      TRACE(4,"    Checking candidate: [line "+util::int2string(i->line)+"] "+util::int2string(i->weight)+" "+util::set2string(i->enabling_flags,"|")+" "+(i->context_neg?"not:":"")+i->context+" ("+i->leftChk+util::list2string(i->leftConds,"")+","+i->rightChk+util::list2string(i->rightConds,"")+") "+i->operation+" "+i->newNode1+":"+i->newNode2+" +("+util::set2string(i->flags_toggle_on,"/")+") -("+util::set2string(i->flags_toggle_off,"/")+")");

      // check extra conditions on chunks and context
      if (enabled_rule(*i)  
          && matching_condition(trees[chk]->begin(),i->leftConds,i->leftRE) 
	  && matching_condition(trees[chk+1]->begin(),i->rightConds,i->rightRE) 
          && matching_context(trees,chk,*i)
          && matching_operation(trees,chk,*i)) {
     
	if (bprio == -1 || bprio>i->weight) {
	  found = true;
	  best=i;
	  bprio=i->weight;
	}

	TRACE(3,"    Candidate: [line "+util::int2string(i->line)+"] "+util::int2string(i->weight)+" "+util::set2string(i->enabling_flags,"|")+" "+(i->context_neg?"not:":"")+i->context+" ("+i->leftChk+util::list2string(i->leftConds,"")+","+i->rightChk+util::list2string(i->rightConds,"")+") "+i->operation+" "+i->newNode1+":"+i->newNode2+" +("+util::set2string(i->flags_toggle_on,"/")+") -("+util::set2string(i->flags_toggle_off,"/")+")  -- MATCH");
      }
      else  {
	TRACE(3,"    Candidate: [line "+util::int2string(i->line)+"] "+util::int2string(i->weight)+" "+util::set2string(i->enabling_flags,"|")+" "+(i->context_neg?"not:":"")+i->context+" ("+i->leftChk+util::list2string(i->leftConds,"")+","+i->rightChk+util::list2string(i->rightConds,"")+") "+i->operation+" "+i->newNode1+":"+i->newNode2+" +("+util::set2string(i->flags_toggle_on,"/")+") -("+util::set2string(i->flags_toggle_off,"/")+")  -- no match");
      }
    }
  }

  if (found) 
    return (*best);
  else {
    TRACE(3,"    NO matching candidates found, applying default rule.");    
    // Default rule: top_left, no relabel
    return completerRule("-","-","top_left");
  }
  
}


///////////////////////////////////////////////////////////////
/// apply a tree completion rule
///////////////////////////////////////////////////////////////

parse_tree * completer::applyRule(const completerRule & r, parse_tree * chunkLeft, parse_tree * chunkRight) {
  
  // toggle necessary flags on/off
  set<string>::iterator x;
  for (x=r.flags_toggle_on.begin(); x!=r.flags_toggle_on.end(); x++) 
    active_flags.insert(*x);
  for (x=r.flags_toggle_off.begin(); x!=r.flags_toggle_off.end(); x++) 
    active_flags.erase(*x);

  // hang left tree under right tree root
  if (r.operation=="top_right") {
    TRACE(3,"Applying rule: Insert left chunk under top_right");
    // Right is head
    chunkLeft->begin()->info.set_head(false);      
    chunkRight->begin()->info.set_head(true);

    // change node labels if required
    if (r.newNode1!="-") {
      TRACE(3,"    ... and relabel left chunk (child) as "+r.newNode1);
      chunkLeft->begin()->info.set_label(r.newNode1);      
    }
    if (r.newNode2!="-") {
      TRACE(3,"    ... and relabel right chunk (parent) as "+r.newNode2);
      chunkRight->begin()->info.set_label(r.newNode2);   
    }   

    // insert Left tree under top node in Right
    chunkRight->hang_child(*chunkLeft,false);
    return chunkRight;
   }
  
  // hang right tree under left tree root and relabel root
  else if (r.operation=="top_left") {
    TRACE(3,"Applying rule: Insert right chunk under top_left");
    // Left is head
    chunkRight->begin()->info.set_head(false);
    chunkLeft->begin()->info.set_head(true);

    // change node labels if required
    if (r.newNode1!="-") {
      TRACE(3,"    ... and relabel left chunk (parent) as "+r.newNode1);
      chunkLeft->begin()->info.set_label(r.newNode1);      
    }   
    if (r.newNode2!="-") {
      TRACE(3,"    ... and relabel right chunk (child) as "+r.newNode2);
      chunkRight->begin()->info.set_label(r.newNode2);   
    }

    // insert Right tree under top node in Left
    chunkLeft->hang_child(*chunkRight);
    return chunkLeft;
  }

  // hang right tree under last node in left tree
  else if (r.operation=="last_left") {
    TRACE(3,"Applying rule: Insert right chunk under last_left with label "+r.newNode1);
    // Left is head, so unmark Right as head.
    chunkRight->begin()->info.set_head(false); 
    // obtain last node with given label in Left tree
    // We stored it in the rule when checking for its applicability.
    parse_tree::iterator p=r.last;
    // hang Right tree under last node in Left tree
    p->hang_child(*chunkRight); 
    return chunkLeft;
  }
  
  // hang left tree under 'last' node in right tree
  else if (r.operation=="last_right") {
    TRACE(3,"Applying rule: Insert left chunk under last_right with label "+r.newNode1);
    // Right is head, so unmark Left as head.
    chunkLeft->begin()->info.set_head(false); 
    // obtain last node with given label in Right tree
    // We stored it in the rule when checking for its applicability.
    parse_tree::iterator p=r.last;
    // hang Left tree under last node in Right tree
    p->hang_child(*chunkLeft,false); 
    return chunkRight;
  }

  // hang right tree where last node in left tree is, and put the later under the former
  else if (r.operation=="cover_last_left") {
    TRACE(3,"Applying rule: Insert right chunk to cover_last_left with label "+r.newNode1);
    // Right will be the new head
    chunkRight->begin()->info.set_head(true); 
    chunkLeft->begin()->info.set_head(false); 
    // obtain last node with given label in Left tree
    // We stored it in the rule when checking for its applicability.
    parse_tree::iterator last=r.last;
    parse_tree::iterator parent = last->get_parent();
    // put last_left tree under Right tree (removing it from its original place)
    chunkRight->hang_child(*last); 
    // put chunkRight under the same parent that last_left
    parent->hang_child(*chunkRight);
    return chunkLeft;
  }

  else {
    ERROR_CRASH("Internal Error unknown rule operation type: "+r.operation);
    return NULL; // avoid compiler warnings
  }
  
}


//---------- Class depLabeler ----------------------------------

///////////////////////////////////////////////////////////////
/// Constructor: create dependency parser
///////////////////////////////////////////////////////////////

depLabeler::depLabeler(const string &filename) : semdb(NULL) {

  int lnum=0;
  string path=filename.substr(0,filename.find_last_of("/\\")+1);
  string sf,wf,line;

  ifstream fin;

  fin.open(filename.c_str());  
  if (fin.fail()) ERROR_CRASH("Cannot open the labeler rules file "+filename);

  check_wordclass::wordclasses.clear();
  int reading=0; 
  while (getline(fin,line)) {
    lnum++;
    
    if (line.empty() || line[0]=='%') {} // ignore comment and empty lines

    else if (line == "<CLASS>") reading=1;
    else if (line == "</CLASS>") reading=0;

    else if (line == "<GRLAB>") reading=2;
    else if (line == "</GRLAB>") reading=0;

    else if (line == "<SEMDB>") reading=3;
    else if (line == "</SEMDB>") {
      reading=0;
      if ( !(sf.empty() && wf.empty()) ) {
	semdb= new semanticDB(sf,wf);
	TRACE(3,"depLabeler loaded SemDB");
      }
    }

    else if (reading==1) {
      // load CLASS section
      string vclass, vlemma;
      istringstream sin;  sin.str(line);
      sin>>vclass>>vlemma;
      
      if (vlemma[0]=='"' && vlemma[vlemma.size()-1]=='"') {
	// Class defined in a file, load it.
	string fname= util::absolute(vlemma,path);  // if relative, use main file path as base
	
	ifstream fclas;
	fclas.open(fname.c_str());  	
	if (fclas.fail()) ERROR_CRASH("Cannot open word class file "+fname);
	
	while (getline(fclas,line)) {
	  if(!line.empty() && line[0]!='%') {
	    istringstream sline;
	    sline.str(line);
	    sline>>vlemma;
	    check_wordclass::wordclasses.insert(vclass+"#"+vlemma);
	  }
	}
	fclas.close();
      }
      else {
	// Enumerated class, just add the word
	check_wordclass::wordclasses.insert(vclass+"#"+vlemma);
      }       
    }
    else if (reading==2) {
      ////// load GRLAB section defining labeling rules
      check_and * expr= new check_and();

      // Read first word in line
      string s;
      istringstream sin;  sin.str(line);
      sin>>s;
      if (s=="UNIQUE") {
	// "UNIQUE" key found, add labels in the line to the set
	string lab; 
	while (sin>>lab) unique.insert(lab);
      }
      else {
	// not "UNIQUE" key, it is a normal rule.
	ruleLabeler r;
	r.line=lnum;
	r.ancestorLabel=s;  // we had already read the first word.
	sin>>r.label;       // second word is the label
	
	TRACE(4,"RULE FOR:"+r.ancestorLabel+" -> "+r.label);
	string condition;
	while (sin>>condition) 
	  expr->add(build_expression(condition));
	
	r.re=expr;
	rules[r.ancestorLabel].push_back(r);      
      }
    }
    else if (reading==3) {
      ////// load SEMDB section 
      istringstream sin;  sin.str(line);
      string key,fname;
      sin>>key>>fname;
      
      if (key=="SenseFile")   sf= util::absolute(fname,path); 
      else if (key=="WNFile") wf= util::absolute(fname,path); 
      else 
	WARNING("Unknown parameter "+key+" in SEMDB section of file "+filename+". SemDB not loaded");      
    }
  }

  TRACE(1,"depLabeler successfully created");
}

///////////////////////////////////////////////////////////////
/// Constructor: create dependency parser
///////////////////////////////////////////////////////////////

depLabeler::~depLabeler() {
  delete semdb;
}


///////////////////////////////////////////////////////////////
/// Constructor private method: parse conditions and build rule expression
///////////////////////////////////////////////////////////////

rule_expression* depLabeler::build_expression(const string &condition) {

  rule_expression *re=NULL;
  // parse condition and obtain components
  string::size_type pos=condition.find('=');
  string::size_type pdot=condition.rfind('.',pos);
  if (pos!=string::npos && pdot!=string::npos) {
    // Disassemble the condition. E.g. for condition "p.class=mov" 
    // we get: node="p";  func="class"; value="mov"; negated=false
    bool negated = (condition[pos-1]=='!');
    string node=condition.substr(0,pdot);
    string func=condition.substr(pdot+1, pos-pdot-1-(negated?1:0) );
    string value=condition.substr(pos+1);
    TRACE(4,"  adding condition:("+node+","+func+","+value+")");    

    // check we do not request impossible things
    if (semdb==NULL && (func=="tonto" || func=="semfile" || func=="synon" || func=="asynon")) {
      ERROR_CRASH("Semantic function '"+func+"' was used in labeling rules, but no previous <SEMDB> section was found. Make sure <SEMDB> section is defined before <GRLAB> section.");
    }

    // if the check is negated and we have As or Es, we must invert them
    if (negated) {
      if (node[0]=='A') node[0]='E';
      else if (node[0]=='E') node[0]='A';
    }

    // create rule checker for function requested in condition
    if (func=="label")        re=new check_category(node,value);
    else if (func=="side")    re=new check_side(node,value);
    else if (func=="lemma")   re=new check_lemma(node,value);
    else if (func=="pos")     re=new check_pos(node,value);
    else if (func=="class")   re=new check_wordclass(node,value);
    else if (func=="tonto")   re=new check_tonto(*semdb,node,value);
    else if (func=="semfile") re=new check_semfile(*semdb,node,value);
    else if (func=="synon")   re=new check_synon(*semdb,node,value);
    else if (func=="asynon")  re=new check_asynon(*semdb,node,value);
    else 
      WARNING("Error reading dependency rules. Ignored unknown function "+func);
    
    if (negated) re=new check_not(re);
  } 
  else 
    WARNING("Error reading dependency rules. Ignored incorrect condition "+condition);

  return re;
}



///////////////////////////////////////////////////////////////
/// Label nodes in a depencendy tree. (Initial call)
///////////////////////////////////////////////////////////////

void depLabeler::label(dep_tree * dependency) {
  TRACE(2,"------ LABELING Dependences ------");
  dep_tree::iterator d=dependency->begin(); 
  d->info.set_label("top");
  label(dependency, d);
}


///////////////////////////////////////////////////////////////
/// Label nodes in a depencendy tree. (recursive)
///////////////////////////////////////////////////////////////

void depLabeler::label(dep_tree* dependency, dep_tree::iterator ancestor) {

  dep_tree::sibling_iterator d,d1;

  // there must be only one top 
  for (d=ancestor->sibling_begin(); d!=ancestor->sibling_end(); ++d) {
          
    ///const string ancestorLabel = d->info.get_dep_result();
    const string ancestorLabel = ancestor->info.get_link()->info.get_label();
    TRACE(2,"Labeling dependency: "+d->info.get_link()->info.get_label()+" --> "+ancestorLabel);
    
    map<string, list <ruleLabeler> >::iterator frule=rules.find(ancestorLabel);
    if (frule!=rules.end()) {
      list<ruleLabeler>::iterator rl=frule->second.begin();
      bool found=false;

      while (rl!=frule->second.end() && !found) {

	TRACE(3,"  Trying rule: [line "+util::int2string(rl->line)+"] ");
	bool skip=false;
	// if the label is declared as unique and a sibling already has it, skip the rule.
        if (unique.find(rl->label)!=unique.end())
	  for (d1=ancestor->sibling_begin(); d1!=ancestor->sibling_end() && !skip; ++d1)
	    skip = (rl->label == d1->info.get_label());
	
	if (!skip) {
	  found = (rl->check(ancestor,d)); 
	  if (found) { 
	    d->info.set_label(rl->label);
	    TRACE(3,"      [line "+util::int2string(rl->line)+"] "+rl->ancestorLabel+" "+rl->label+" -- rule matches!");
	    TRACE(2,"      RULE APPLIED. Dependence labeled as "+rl->label);
	  }
	  else {
	    TRACE(3,"      [line "+util::int2string(rl->line)+"] "+rl->ancestorLabel+" "+rl->label+" -- no match");
	  }
	}
	else {
	  TRACE(3,"      RULE SKIPPED -- Unique label "+rl->label+" already present.");
	}

	++rl;
      }
       
      if (!found) {d->info.set_label("modnomatch");}
    }
    else { 
      d->info.set_label("modnorule");
    }     
   
    // Recursive call
    label(dependency, d);
  }
}


//---------- Class dependencyMaker ----------------------------------

///////////////////////////////////////////////////////////////
/// constructor. Load a dependecy rule file.
///////////////////////////////////////////////////////////////

dep_txala::dep_txala(const string & fullgram, const string & startSymbol) : comp(fullgram), labeler(fullgram), start(startSymbol) {}


///////////////////////////////////////////////////////////////
/// Enrich all sentences in given list with a depenceny tree.
///////////////////////////////////////////////////////////////

void dep_txala::analyze(list<sentence> & ls) {
list<sentence>::iterator s;

  for (s=ls.begin(); s!=ls.end(); ++s) {

    parse_tree buff = s->get_parse_tree();
    
    parse_tree ntr = comp.complete(buff,start);
    s->set_parse_tree(ntr);

    // PrintTree(ntr.begin(),0); // debugging

    // We need to keep the original parse tree as we are storing iterators!!
    dep_tree* deps = dependencies(s->get_parse_tree().begin(),s->get_parse_tree().begin());
    // Set labels on the dependencies
    labeler.label(deps);
    
    // return the tree
    s->set_dep_tree(*deps);
    delete(deps);
  }
}


///////////////////////////////////////////////////////////////
/// Enrich all sentences in given list with a depenceny tree
/// and return a copy. Useful for Perl API
///////////////////////////////////////////////////////////////

list<sentence> dep_txala::analyze(const list<sentence> & ls) {
list<sentence> x;

  x=ls;
  analyze(x);
  return(x);
}


//---------- dependencyMaker private functions 

///////////////////////////////////////////////////////////////
/// Obtain a depencendy tree from a parse tree.
///////////////////////////////////////////////////////////////

dep_tree * dep_txala::dependencies(parse_tree::iterator tr, parse_tree::iterator link) {

  dep_tree * result;

  if (tr->num_children() == 0) { 
    // direct case. Leaf node, just build and return a one-node dep_tree.
    depnode d(tr->info); 
    d.set_link(link);
    result = new dep_tree(d);
  }
  else {
    // Recursive case. Non-leaf node. build trees 
    // for all children and hang them below the head

    // locate head child
    parse_tree::sibling_iterator head;
    parse_tree::sibling_iterator k;
    for (k=tr->sibling_begin(); k!=tr->sibling_end() && !k->info.is_head(); ++k);
    if (k==tr->sibling_end()) {
      WARNING("NO HEAD Found!!! Check your chunking grammar and your dependency-building rules.");
      k=tr->sibling_begin();
    }
    head = k;
    
    // build dep tree for head child
    if (!tr->info.is_head()) link=tr;
    result = dependencies(head,link);
    
    // Build tree for each non-head child and hang it under the head.
    // We maintain the original sentence order (not really necessary,
    // but trees are cuter this way...)

    // children to the left of the head
    k=head; ++k;
    while (k!=tr->sibling_end()) { 
      if (k->info.is_head()) WARNING("More than one HEAD detected. Only first prevails.");
      dep_tree *dt = dependencies(k,k);
      result->hang_child(*dt);  // hang it as last child
      ++k;
    }
    // children to the right of the head
    k=head; --k;
    while (k!=tr->sibling_rend()) { 
      if (k->info.is_head()) WARNING("More than one HEAD detected. Only first prevails.");
      dep_tree *dt = dependencies(k,k);
      result->hang_child(*dt,false);   // hang it as first child
      --k;
    }
  }

  // copy chunk information from parse tree
  result->info.set_chunk(tr->info.get_chunk_ord());  
  return (result);
}

