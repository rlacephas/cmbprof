//===- llvm/Analysis/EdgeDominatorTree.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EDGE_DOMINATOR_TREE_H
#define LLVM_EDGE_DOMINATOR_TREE_H

#include "llvm/BasicBlock.h"
#include <set>
#include <map>
#include <list>

// short version of set_intersection for IndexSets
#define INTERSECT(s1, s2, outSet) set_intersection(s1.begin(), s1.end(), s2.begin(), s2.end(), std::insert_iterator<IndexSet>(outSet,outSet.begin()))


namespace llvm {
  class Module;
	struct EdgeNode;

  typedef unsigned EdgeIndex;

  typedef std::set<EdgeIndex> IndexSet;
  typedef std::set<EdgeIndex>::iterator IndexSetIterator;

  typedef std::map<EdgeIndex,EdgeNode*> EdgeNodeMap;
  typedef std::map<EdgeIndex,EdgeNode*>::iterator EdgeNodeMapIterator;

  typedef std::list<EdgeIndex> IndexList;
  typedef std::list<EdgeIndex>::iterator IndexListIterator;

  typedef std::vector<EdgeIndex> IndexVector;
  
  typedef std::map<EdgeIndex,IndexSet> IndexSetMap;
  typedef std::map<EdgeIndex,IndexSet>::iterator IndexSetMapIterator;

  // a CFG edge node
	struct EdgeNode {
		BasicBlock* source;
		BasicBlock* target;
    EdgeIndex index;
    IndexSet children;
    IndexSet parents;
    // Edge dominance info
    IndexSet domChildren;
    EdgeIndex domIndex;
	};

  class CFGEdgeDomTree {
  public:

    CFGEdgeDomTree(Function& F, EdgeIndex firstEdge);
    ~CFGEdgeDomTree();

    // the caller of getEdgeMap takes on the responsibility of
    // deallocating EdgeNode pointers in _edges.  Supply 'this' as who
    EdgeNodeMap* claimEdgeMap(void* who);
    // caller revokes ownership of EdgeMap.  Supply 'this' as who
    void unclaimEdgeMap(void* who);

  protected:

    // worklist to help with worklist algorithms, mostly handling of _pending
    class Worklist {
    public:
      Worklist(EdgeNodeMap& edges, IndexSet& nonBackEdges);
      EdgeIndex pop();
      void pushReady(IndexSet& children);
      void push(EdgeIndex edge){_worklist.push_back(edge);};
      bool empty(){return(_worklist.empty());};
      void print(llvm::raw_ostream& stream);
    private:
      IndexList _worklist;                     // the worklist
      std::map<EdgeIndex, int> _pending;       // # of unprocessed parents
    };
    
    std::set<void*> _edgesTakenBy;    // who has claimed our pointers?
    EdgeNodeMap _edges;
    IndexSet _roots;
    IndexSet _nonBackEdges;
    IndexSetMap _ancestorSets;
    EdgeIndex _minEdgeIndex;
    EdgeIndex _maxEdgeIndex;
    
    void buildGraph();
    void findRoots();
    void findNonBackEdges(const EdgeNode& root, IndexSet& visited, 
                          IndexSet& currPath);
    void computeAncestorSets();
    void computeEdgeDominance();

    void resetPending();

    void printIndexSet(llvm::raw_ostream& strem, IndexSet& set);
    void intersect(IndexSet& s1, IndexSet& s2, IndexSet& outSet);

  };


  class EdgeDominatorTree {
  public:
		EdgeDominatorTree(Module& M);
    ~EdgeDominatorTree();
    
		unsigned getDominatorIndex(EdgeIndex e);
		unsigned getEdgeCount();
    unsigned getDepth(EdgeIndex e);
    
		void writeToFile(std::string filename);

    void printDominance(llvm::raw_ostream& stream, EdgeNodeMap& edges);
    
	private:
    EdgeNodeMap _edges;
  };
} // End llvm namespace

#endif
