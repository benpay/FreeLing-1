//////////////////////////////////////////////////////////////////
//
//    FreeLing - Open Source Language Analyzers
//
//    Copyright (C) 2004   TALP Research Center
//                         Universitat Politecnica de Catalunya
//
//    This library is free software; you can redistribute it and/or
//    modify it under the terms of the GNU Lesser General Public
//    License as published by the Free Software Foundation; either
//    version 2.1 of the License, or (at your option) any later version.
//
//    This library is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//    Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public
//    License along with this library; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
//
//    contact: Lluis Padro (padro@lsi.upc.es)
//             TALP Research Center
//             despatx C6.212 - Campus Nord UPC
//             08034 Barcelona.  SPAIN
//
////////////////////////////////////////////////////////////////


//------------------------------------------------------------------//
//
//                    IMPORTANT NOTICE
//
//  This file contains a simple main program to illustrate 
//  usage of FreeLing analyzers library.
//
//  This sample main program may be used straightforwardly as 
//  a basic front-end for the analyzers (e.g. to analyze corpora)
//
//  Neverthless, if you want embed the FreeLing libraries inside
//  a larger application, or you want to deal with other 
//  input/output formats (e.g. XML), the efficient and elegant 
//  way to do so is consider this file as a mere example, and call 
//  the library from your your own main code.
//
//------------------------------------------------------------------//



using namespace std;

#include <sstream>
#include <iostream>

#include <map>
#include <vector>

#include "freeling.h"
#include "LKBconfig.h"

const char FORMFEED=0x0C;


//---------------------------------------------
// output a string both in cout and cerr
//---------------------------------------------
void say(const string &s) {
  cout<<s<<endl;
  cerr<<s<<endl;
}

//---------------------------------------------
// print obtained analysis.
//---------------------------------------------
void PrintResults(const list<sentence> &ls, const config &cfg) {
  word::const_iterator ait;
  sentence::const_iterator w;
  list<sentence>::const_iterator is;
  parse_tree tr;  
  dep_tree dep;
  int nsentence=1;
 
 
  for (is=ls.begin(); is!=ls.end(); is++,++nsentence) {
   
    say("<segment>");
    for (w=is->begin(); w!=is->end(); w++) {
      say("  <token form=\""+w->get_form()+"\" from=\""+util::int2string(w->get_span_start())+"\" to=\""+util::int2string(w->get_span_finish())+"\" >");

      if (cfg.OutputFormat==MORFO || cfg.OutputFormat==TAGGED) {

        // iterate over selected tags (all when MORFO, only one when TAGGED)
	for(ait=w->selected_begin(); ait!=w->selected_end(); ait++){
	  
          char c1=ait->get_parole()[0];
          char c2=ait->get_parole()[1];
	  
	  if (ait->is_retokenizable()) {            
	    say("    <analysis stem=\""+ait->get_lemma()+"\" >");
	    list<word> rtk=ait->get_retokenizable();
	    list<word>::iterator r;
            string form;
	    for (r=rtk.begin(); r!= rtk.end(); r++) {
              if(r == rtk.begin()) 
                form = r->get_form();
              else
                form = form + r->get_form();
              say("      <rule id=\""
                  + string(r == rtk.begin() ? "" : "+") + r->get_parole() 
                  + "\" form=\"" + form +"\" />");
	    }
	    say("    </analysis>");
	  }
	  else if (c1!='Z' && c1!='W' && c1!='F' && !(c1=='N' && c2=='P') && !(c1=='A' && c2=='O')) {
	    say("    <analysis stem=\""+ait->get_lemma()+"\" >");
	    say("      <rule id=\""+ait->get_parole()+"\" form=\""+w->get_form()+"\" />");
	    say("    </analysis>");
	  }
	  else if (c1=='Z' && (w->get_form()=="un")) {
            say("    <analysis stem=\"un\" >");
            say("      <rule id=\""+ait->get_parole()+"\" form=\""+w->get_form()+"#"+ait->get_lemma()+"\" />");
            say("    </analysis>");
	  }
	  else if (c1=='Z' && (w->get_form()=="una")) {
            say("    <analysis stem=\"una\" >");
            say("      <rule id=\""+ait->get_parole()+"\" form=\""+w->get_form()+"#"+ait->get_lemma()+"\" />");
            say("    </analysis>");
	  }
	  else if (c1=='Z' && (w->get_form()=="uno")) {
            say("    <analysis stem=\"uno\" >");
            say("      <rule id=\""+ait->get_parole()+"\" form=\""+w->get_form()+"#"+ait->get_lemma()+"\" />");
            say("    </analysis>");
	  }
	  else {            
	    say("    <analysis stem=\""+ait->get_parole()+"\" >");
	    say("      <rule id=\""+ait->get_parole()+"\" form=\""+w->get_form()+"#"+ait->get_lemma()+"\" />");
	    say("    </analysis>");
	  }
	}
      }
      say("  </token>");
    }
    say("</segment>");
  }

}

//---------------------------------------------
// Plain text, start with tokenizer.
//---------------------------------------------
void ProcessPlain(const config &cfg, tokenizer *tk, splitter *sp, maco *morfo, POS_tagger *tagger, nec* neclass) {
  string text;
  list<word> av;
  list<word>::const_iterator i;
  list<sentence> ls;

    bool head=false;
    while (std::getline(std::cin,text)) {

      cerr<<"   ## Read line ("<<text<<")"<<endl;
      if (!head) {
	unsigned int p=text.find("<?xml version='1.0' encoding='iso-8859-1'?>"); 
	if (p!=string::npos) {
	  text.erase(p,43);
	  say("<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>");
          head=true;
	}
	else ERROR_CRASH("ERROR - <?xml?> header expected");
      }

      if (text[0]==FORMFEED) {
	// process last sentence in buffer (if any)
	ls=sp->split(av, true);  //flush splitter buffer
	morfo->analyze(ls);
	if (cfg.OutputFormat>=TAGGED) tagger->analyze(ls);
	if (cfg.OutputFormat>=TAGGED && cfg.NEC_NEClassification) neclass->analyze(ls);
	
	PrintResults(ls, cfg);
        cout<<FORMFEED<<endl; 
        cerr<<FORMFEED<<endl; 
        head=false;
      }
      else {
	// clean xml tags from input
	unsigned int p;
	p=text.find("<text>"); if (p!=string::npos) text.erase(p,6);
	p=text.find("</text>"); if (p!=string::npos) text.erase(p,7);
	
	av=tk->tokenize(text);
	ls=sp->split(av, cfg.AlwaysFlush);
	morfo->analyze(ls);
	if (cfg.OutputFormat>=TAGGED) tagger->analyze(ls);
	if (cfg.OutputFormat>=TAGGED && cfg.NEC_NEClassification) neclass->analyze(ls);

	PrintResults(ls, cfg);	
	av.clear(); // clear list of words for next use
	ls.clear(); // clear list of sentences for next use
      }
    }

}



//---------------------------------------------
// Sample main program
//---------------------------------------------
int main(int argc, char **argv) {

  // we use pointers to the analyzers, so we
  // can create only those strictly necessary.
  tokenizer *tk=NULL;
  splitter *sp=NULL;
  maco *morfo=NULL;
  nec *neclass=NULL;
  POS_tagger *tagger=NULL;

  // read configuration file and command-line options
  config cfg(argv);

  // create required analyzers
  tk = new tokenizer(cfg.TOK_TokenizerFile); 
  sp = new splitter(cfg.SPLIT_SplitterFile);

  // the morfo class requires several options at creation time.
  // they are passed packed in a maco_options object.
  maco_options opt(cfg.Lang);
  // boolean options to activate/desactivate modules
  // default: all modules activated (options set to "false")
  opt.set_active_modules(cfg.MACO_SuffixAnalysis,   cfg.MACO_MultiwordsDetection, 
			 cfg.MACO_NumbersDetection, cfg.MACO_PunctuationDetection, 
			 cfg.MACO_DatesDetection,   cfg.MACO_QuantitiesDetection,
			 cfg.MACO_DictionarySearch, cfg.MACO_ProbabilityAssignment,
			 cfg.MACO_NERecognition);
  // decimal/thousand separators used by number detection
  opt.set_nummerical_points(cfg.MACO_Decimal, cfg.MACO_Thousand);
  // Minimum probability for a tag for an unkown word
  opt.set_threshold(cfg.MACO_ProbabilityThreshold);
  // Data files for morphological submodules. by default set to ""
  // Only files for active modules have to be specified 
  opt.set_data_files(cfg.MACO_LocutionsFile,   cfg.MACO_QuantitiesFile,   cfg.MACO_SuffixFile, 
		     cfg.MACO_ProbabilityFile, cfg.MACO_DictionaryFile, cfg.MACO_NPdataFile,
		     cfg.MACO_PunctuationFile);
  // create analyzer with desired options
  morfo = new maco(opt);

  if (cfg.OutputFormat>=TAGGED) {
     if (cfg.TAGGER_which == HMM)
       tagger = new hmm_tagger(cfg.Lang, cfg.TAGGER_HMMFile,
                               cfg.TAGGER_Retokenize, cfg.TAGGER_ForceSelect);
     else if (cfg.TAGGER_which == RELAX)
       tagger = new relax_tagger(cfg.TAGGER_RelaxFile, cfg.TAGGER_RelaxMaxIter,
                                 cfg.TAGGER_RelaxScaleFactor,
                                 cfg.TAGGER_RelaxEpsilon,
                                 cfg.TAGGER_Retokenize,
                                 cfg.TAGGER_ForceSelect);
  }

  if (cfg.OutputFormat>=TAGGED && cfg.NEC_NEClassification) {
    neclass = new nec("NP", cfg.NEC_FilePrefix);
  }

  // Input is plain text.
  ProcessPlain(cfg,tk,sp,morfo,tagger,neclass);

  // clean up. Note that deleting a null pointer is a safe (yet useless) operation
  delete tk;
  delete sp; 
  delete morfo; 
  delete tagger;
  delete neclass; 
}
