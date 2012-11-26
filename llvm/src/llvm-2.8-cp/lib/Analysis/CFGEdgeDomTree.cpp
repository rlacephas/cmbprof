//===- EdgeDominatorTree.cpp ----------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "generate-edge-dominance"

#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/Analysis/EdgeDominatorTree.h"
// PB?? #include "llvm/Analysis/Passes.h"
// PB?? #include "llvm/Support/Debug.h"
// PB?? #include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;


CFGEdgeDomTree::CFGEdgeDomTree(Function &F, EdgeIndex firstEdge) 
{
  EdgeIndex edgeCounter = firstEdge;

  if(F.isDeclaration())
    return;
  
  // Add the entry edge
  EdgeNode* entryEdge = new EdgeNode;
  entryEdge->source = 0;
  entryEdge->target = &(F.getEntryBlock());
  entryEdge->domIndex = edgeCounter; // entry edge self-dominates by def'n
  entryEdge->index = edgeCounter++;
  
  _edges[entryEdge->index] = entryEdge;
  
  //errs() << "edt: "<< F.getName() << " entry edge: " 
  //       << entryEdge->index << " (" << F.size() << " blocks)\n";
  
  // Add all edges to the edge map
  for( Function::iterator BB = F.begin(), E = F.end(); BB != E; BB++ ) 
  {
    TerminatorInst* TI = BB->getTerminator();
    
    for( unsigned s = 0, e = TI->getNumSuccessors(); s != e; s++ ) 
    {
      EdgeNode* newEdge = new EdgeNode;
      newEdge->source = BB;
      newEdge->target = TI->getSuccessor(s);
      newEdge->index = edgeCounter++;
      _edges[newEdge->index] = newEdge;
    }
  }
  
  // analyse the edges to find the dominance relationships
  buildGraph();            // link edges to successors
  findRoots();             // find all entry edges in case this is a forest

  // find all non-back edges; needed for next computations
  IndexSet emptyPath, emptyVisited;
  for(IndexSetIterator root = _roots.begin(), rootEnd = _roots.end();
      root != rootEnd; root++)
  {
    findNonBackEdges(*_edges[*root], emptyPath, emptyVisited);
  }

  computeAncestorSets();   // Least common ancestor of parents is idom
  computeEdgeDominance();  // Find the idom of each edge

} // CFGEdgeDomTree(Function&, EdgeIndex)


CFGEdgeDomTree::~CFGEdgeDomTree() 
{
  if(_edgesTakenBy.empty())  // only deallocate if we're the only owner
		for( EdgeNodeMapIterator M = _edges.begin(), E = _edges.end();
         M != E; M++ )
			delete M->second;
}


// the caller of getEdgeMap takes on the responsibility of
// deallocating EdgeNode pointers in _edges.  Supply 'this' as who
EdgeNodeMap* CFGEdgeDomTree::claimEdgeMap(void* who)
{
  _edgesTakenBy.insert(who);
  return(&_edges);
}

// caller revokes ownership of EdgeMap.  Supply 'this' as who
void CFGEdgeDomTree::unclaimEdgeMap(void* who)
{
  _edgesTakenBy.erase(who);
}


// Compute predecessor/ successor information - this is stupid and slow,
// but the best for the current edge-numbering scheme
void CFGEdgeDomTree::buildGraph()
{
  for( EdgeNodeMapIterator edge = _edges.begin(), edgeEnd = _edges.end();
       edge != edgeEnd; edge++ ) 
  {
    for( EdgeNodeMapIterator succ = _edges.begin(), succEnd = _edges.end();
         succ != succEnd; succ++ ) 
    {
      EdgeNode* parent = edge->second;
      EdgeNode* child = succ->second;
      if( parent->target == child->source) 
      {
        parent->children.insert(child->index);
        child->parents.insert(parent->index);
      }
    }
  } 
} // buildGraph



// recursive, depth-first walk of the graph (in 'edgeMap') from 'root'
// to find non-backedges.  Initially, 'edges' should be created and
// contain only known non-backedges. On completion, 'edges' contains
// all non-backedges reachable from 'root'.  visited tracks visited
// edges; currPath track the current path.
void CFGEdgeDomTree::findNonBackEdges(const EdgeNode& root, 
                                      IndexSet& visited, 
                                      IndexSet& currPath)
{
  // use a stack AND a set for the path: that's probably more
  // expensive that recursion!

  // if src == target: must be a back edge
  if(root.source == root.target)
    return;

  // don't duplicate work if we've already been here
  if(visited.count(root.index) > 0)
    return;

  visited.insert(root.index);
  currPath.insert(root.index);
  
  // if any successor on path: must be a back edge
  for(IndexSetIterator child = root.children.begin(), 
         childEnd = root.children.end(); child != childEnd; child++ )
  {
    if(currPath.count(*child) > 0)
    {
      currPath.erase(root.index);
      return;
    }
  }

  // no successors in on path, so not a back edge.
  _nonBackEdges.insert(root.index);
  for(IndexSetIterator child = root.children.begin(), 
         childEnd = root.children.end(); child != childEnd; child++ )
  {
    findNonBackEdges(*_edges[*child], visited, currPath);
  }

  // returning from recursion; not on path anymore
  currPath.erase(root.index);
} //findNonBackEdges



// print the set contents
void CFGEdgeDomTree::printIndexSet(llvm::raw_ostream& stream, IndexSet& set)
{
  for(IndexSetIterator I = set.begin(), E = set.end(); I != E; I++)
  {
    stream << *I << " ";
  }
  stream << "\n";
}


// find all edges in map that have no parents
// assumes buildGraph()
void CFGEdgeDomTree::findRoots()
{
  for(EdgeNodeMapIterator nodePair = _edges.begin(), nodeEnd = _edges.end();
      nodePair != nodeEnd; nodePair++)
  {
    EdgeNode* node = nodePair->second;
    if(node->parents.size() == 0)
      _roots.insert(node->index);
  }

  // sanity checks
  if(_roots.size() == 0)
    errs() << "error: No roots in node map!\n";

  if(_roots.size() > 1)
    errs() << "warning: multiple roots in CFG!\n";
}


// top-down worklist over edges to build _ancestorSets
// (all non-backedge ancestors of each edge)
// non-strict: an edge is it's own ancestor.
// assumes buildGraph(), findRoots(), findNonBackEdges()
void CFGEdgeDomTree::computeAncestorSets()
{
  Worklist worklist(_edges, _nonBackEdges);

  // seed worklist with roots
  for(IndexSetIterator entry = _roots.begin(), entryEnd = _roots.end();
      entry != entryEnd; entry++)
  {
    worklist.push(*entry);
  }

  while(!worklist.empty())
  {
		// extract a work item
		EdgeIndex currIndex = worklist.pop();
    EdgeNode& currNode = *_edges[currIndex];

    IndexSet ancestors;
    ancestors.insert(currIndex);
    
    // union ancestor sets of all parents
    for(IndexSetIterator parent = currNode.parents.begin(), 
          parentEnd = currNode.parents.end(); parent != parentEnd; parent++)
    {
      IndexSet& pset = _ancestorSets[*parent];
      ancestors.insert(pset.begin(), pset.end());
    }
    // remove non-backedges, store in ancestorSets
    INTERSECT(ancestors, _nonBackEdges, _ancestorSets[currIndex]);

    worklist.pushReady(currNode.children);
  } // !worklist.empty()
} // computeAncestorSets


// Sets each edge's domIndex set to the index of its immediate
// dominator, or to itself if it has no immediate dominator.  Each
// edge's domChildren is populated with the index of all edges it
// immediately dominates.
// assumes buildGraph(), findRoots(), findNonBackEdges(), computeAncestorSets()
void CFGEdgeDomTree::computeEdgeDominance() 
{
  // nothing to do if there are no edges
  if(_edges.empty())
    return;

  // Find LCA (top-down)
  // ancestorSets contain all (non-strict) ancestors
  // immediate dominator is least common (non-strict) ancestor of all parents.

	Worklist worklist(_edges, _nonBackEdges);

  // seed worklist with children of roots
  for(IndexSetIterator entry = _roots.begin(), entryEnd = _roots.end();
      entry != entryEnd; entry++)
  {
    // roots self-dominate
    _edges[*entry]->domIndex = *entry;
    worklist.pushReady(_edges[*entry]->children);
  }

  while( !worklist.empty() )
  {
		// extract a work item
		EdgeIndex currIndex = worklist.pop();
    EdgeNode& currNode = *_edges[currIndex];

    // if only one NBE parent, then dominator trivially determined; done.
    // no change to ancestor set
    IndexSet nbeParents;
    INTERSECT(currNode.parents, _nonBackEdges, nbeParents);
    if( nbeParents.size() == 1 )
    {
      EdgeIndex domIndex = *(nbeParents.begin());
      _edges[currIndex]->domIndex = domIndex;
      _edges[domIndex]->domChildren.insert(currIndex);
      worklist.pushReady(currNode.children);
    }

    // intersect ancestor sets of all parents
    IndexSet ancestors = _ancestorSets[currIndex];  //init with own ancestors
    //errs() << "(" << currIndex << ") In Set:"; 
    //printIndexSet(errs(), ancestors);

    for(IndexSetIterator parent = currNode.parents.begin(), 
          parentEnd = currNode.parents.end(); parent != parentEnd; parent++)
    {
      IndexSet tmp;
      //errs() << "  (" << *parent << "):"; 
      //printIndexSet(errs(), _ancestorSets[*parent]);
      INTERSECT(ancestors, _ancestorSets[*parent], tmp);
      ancestors = tmp;
    }
    // now we have the set of all common (strict) ancestors of all
    // parents.  We can update ancestorSet with reduced set.
    _ancestorSets[currIndex] = ancestors;
    // but we still need to be in our set for our decendents' computation
    // *don't* insert us into ancestors!
    _ancestorSets[currIndex].insert(currIndex);

    //errs() << "(" << currIndex << ") Out Set:";
    //printIndexSet(errs(), ancestors);
    
    // if one ancestor is an ancestor of another, it is not the closest.
    // *don't* apply this pruning to the ancestorSets
    for(IndexSetIterator a1 = ancestors.begin(), a1End = ancestors.end();
        a1 != a1End; a1++)
      for(IndexSetIterator a2 = ancestors.begin(), a2End = ancestors.end();
          a2 != a2End; a2++)
      {
        if(*a1 == *a2) // don't self-prune!
          continue;
        IndexSet& a2ancestors = _ancestorSets[*a2];
        if(a2ancestors.count(*a1) != 0)
        {
          //errs() << "(" << currIndex << ")   Prune: " << *a1 
          //       << " dominates " << *a2 << "\n";
          ancestors.erase(*a1);
        }
      }

    //errs() << "(" << currIndex << ") Pruned Set:";
    //printIndexSet(errs(), ancestors);

    // ancestors holds the parents' least common (non-strict) ancestor

    // sanity checks
    if(ancestors.size() == 0)
    {
      errs() << currIndex << ": LCA leaves no potential dominators!\n  ";
      printIndexSet(errs(), ancestors);
      return;
    }
    if(ancestors.size() > 1)
    {
      errs() << currIndex << ": LCA leaves many potential dominators!\n  ";
      printIndexSet(errs(), ancestors);
      return;
    }

    // ancestors.size() == 1
    EdgeIndex domIndex = *(ancestors.begin());
    _edges[currIndex]->domIndex = domIndex;
    _edges[domIndex]->domChildren.insert(currIndex);

    worklist.pushReady(currNode.children);
  } // !worklist.empty()
}// computeEdgeDominance



// ------------ Worklist inner class ---------------

CFGEdgeDomTree::Worklist::Worklist(EdgeNodeMap& edges, IndexSet& nonBackEdges)
{
  // set _pending to number of non-backedge parents
  for(EdgeNodeMapIterator edge = edges.begin(), edgeEnd = edges.end();
      edge != edgeEnd; edge++)
  {
    IndexSet nbeParents;
    INTERSECT(edge->second->parents, nonBackEdges, nbeParents);
    _pending[edge->first] = nbeParents.size();
  }
}

// remove and return first work item
EdgeIndex CFGEdgeDomTree::Worklist::pop()
{
  EdgeIndex index = _worklist.front();
  _worklist.pop_front();
  return(index);
}

// decrement _pending for all edges in set, and add to worklist if
// nothing pending
void CFGEdgeDomTree::Worklist::pushReady(IndexSet& children)
{
  for(IndexSetIterator child = children.begin(), childEnd = children.end();
      child != childEnd; child++)
  {
    _pending[*child]--;    // allow negative to prevent duplicates

    if(_pending[*child] == 0) 
    {
      _worklist.push_back(*child);
    }
  }
}

void CFGEdgeDomTree::Worklist::print(llvm::raw_ostream& stream)
{
  stream << "(worklist)";
  for(IndexListIterator I = _worklist.begin(), E = _worklist.end(); I != E; I++)
  {
    stream << " " << *I << "(" << _pending[*I] << ")";
  }
  stream << "\n";
}
