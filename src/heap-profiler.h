// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_HEAP_PROFILER_H_
#define V8_HEAP_PROFILER_H_

namespace v8 {
namespace internal {

#ifdef ENABLE_LOGGING_AND_PROFILING

// The HeapProfiler writes data to the log files, which can be postprocessed
// to generate .hp files for use by the GHC/Valgrind tool hp2ps.
class HeapProfiler {
 public:
  // Write a single heap sample to the log file.
  static void WriteSample();

 private:
  // Update the array info with stats from obj.
  static void CollectStats(HeapObject* obj, HistogramInfo* info);
};


// ConstructorHeapProfile is responsible for gathering and logging
// "constructor profile" of JS objects allocated on heap.
// It is run during garbage collection cycle, thus it doesn't need
// to use handles.
class ConstructorHeapProfile BASE_EMBEDDED {
 public:
  ConstructorHeapProfile();
  virtual ~ConstructorHeapProfile() {}
  void CollectStats(HeapObject* obj);
  void PrintStats();
  // Used by ZoneSplayTree::ForEach. Made virtual to allow overriding in tests.
  virtual void Call(String* name, const NumberAndSizeInfo& number_and_size);

 private:
  struct TreeConfig {
    typedef String* Key;
    typedef NumberAndSizeInfo Value;
    static const Key kNoKey;
    static const Value kNoValue;
    static int Compare(const Key& a, const Key& b) {
      // Strings are unique, so it is sufficient to compare their pointers.
      return a == b ? 0 : (a < b ? -1 : 1);
    }
  };
  typedef ZoneSplayTree<TreeConfig> JSObjectsInfoTree;

  ZoneScope zscope_;
  JSObjectsInfoTree js_objects_info_tree_;
};


// JSObjectsCluster describes a group of JS objects that are
// considered equivalent in terms of retainer profile.
class JSObjectsCluster BASE_EMBEDDED {
 public:
  enum SpecialCase {
    ROOTS = 1,
    GLOBAL_PROPERTY = 2
  };

  JSObjectsCluster() : constructor_(NULL), instance_(NULL) {}
  explicit JSObjectsCluster(String* constructor)
      : constructor_(constructor), instance_(NULL) {}
  explicit JSObjectsCluster(SpecialCase special)
      : constructor_(FromSpecialCase(special)), instance_(NULL) {}
  JSObjectsCluster(String* constructor, Object* instance)
      : constructor_(constructor), instance_(instance) {}

  static int CompareConstructors(
      const JSObjectsCluster& a, const JSObjectsCluster& b) {
    // Strings are unique, so it is sufficient to compare their pointers.
    return a.constructor_ == b.constructor_ ? 0
        : (a.constructor_ < b.constructor_ ? -1 : 1);
  }
  static int Compare(const JSObjectsCluster& a, const JSObjectsCluster& b) {
    // Strings are unique, so it is sufficient to compare their pointers.
    const int cons_cmp = CompareConstructors(a, b);
    return cons_cmp == 0 ?
        (a.instance_ == b.instance_ ? 0 : (a.instance_ < b.instance_ ? -1 : 1))
        : cons_cmp;
  }

  bool is_null() const { return constructor_ == NULL; }
  bool can_be_coarsed() const { return instance_ != NULL; }

  void Print(StringStream* accumulator) const;
  // Allows null clusters to be printed.
  void DebugPrint(StringStream* accumulator) const;

 private:
  static String* FromSpecialCase(SpecialCase special) {
    // We use symbols that are illegal JS identifiers to identify special cases.
    // Their actual value is irrelevant for us.
    switch (special) {
      case ROOTS: return Heap::result_symbol();
      case GLOBAL_PROPERTY: return Heap::code_symbol();
      default:
        UNREACHABLE();
        return NULL;
    }
  }

  String* constructor_;
  Object* instance_;
};


struct JSObjectsClusterTreeConfig;
typedef ZoneSplayTree<JSObjectsClusterTreeConfig> JSObjectsClusterTree;

// JSObjectsClusterTree is used to represent retainer graphs using
// adjacency list form. That is, the first level maps JS object
// clusters to adjacency lists, which in their turn are degenerate
// JSObjectsClusterTrees (their values are NULLs.)
struct JSObjectsClusterTreeConfig {
  typedef JSObjectsCluster Key;
  typedef JSObjectsClusterTree* Value;
  static const Key kNoKey;
  static const Value kNoValue;
  static int Compare(const Key& a, const Key& b) {
    return Key::Compare(a, b);
  }
};


class ClustersCoarser BASE_EMBEDDED {
 public:
  ClustersCoarser();

  // Processes a given retainer graph.
  void Process(JSObjectsClusterTree* tree);

  // Returns an equivalent cluster (can be the cluster itself).
  // If the given cluster doesn't have an equivalent, returns null cluster.
  JSObjectsCluster GetCoarseEquivalent(const JSObjectsCluster& cluster);
  // Returns whether a cluster can be substitued with an equivalent and thus,
  // skipped in some cases.
  bool HasAnEquivalent(const JSObjectsCluster& cluster);

  // Used by ZoneSplayTree::ForEach.
  void Call(const JSObjectsCluster& cluster, JSObjectsClusterTree* tree);

 private:
  // Stores a list of back references for a cluster.
  struct ClusterBackRefs {
    explicit ClusterBackRefs(const JSObjectsCluster& cluster_);
    ClusterBackRefs(const ClusterBackRefs& src);
    ClusterBackRefs& operator=(const ClusterBackRefs& src);

    static int Compare(const ClusterBackRefs& a, const ClusterBackRefs& b);

    JSObjectsCluster cluster;
    ZoneList<JSObjectsCluster> refs;
  };
  typedef ZoneList<ClusterBackRefs> SimilarityList;

  // A tree for storing a list of equivalents for a cluster.
  struct ClusterEqualityConfig {
    typedef JSObjectsCluster Key;
    typedef JSObjectsCluster Value;
    static const Key kNoKey;
    static const Value kNoValue;
    static int Compare(const Key& a, const Key& b) {
      return Key::Compare(a, b);
    }
  };
  typedef ZoneSplayTree<ClusterEqualityConfig> EqualityTree;

  static int ClusterBackRefsCmp(
      const ClusterBackRefs* a, const ClusterBackRefs* b) {
    return ClusterBackRefs::Compare(*a, *b);
  }
  int DoProcess(JSObjectsClusterTree* tree);
  int FillEqualityTree();

  static const int INITIAL_BACKREFS_LIST_CAPACITY = 2;
  static const int INITIAL_SIMILARITY_LIST_CAPACITY = 2000;
  // Number of passes for finding equivalents. Limits the length of paths
  // that can be considered equivalent.
  static const int MAX_PASSES_COUNT = 10;

  ZoneScope zscope_;
  SimilarityList simList_;
  EqualityTree eqTree_;
  ClusterBackRefs* currentPair_;
  JSObjectsClusterTree* currentSet_;
};


// RetainerHeapProfile is responsible for gathering and logging
// "retainer profile" of JS objects allocated on heap.
// It is run during garbage collection cycle, thus it doesn't need
// to use handles.
class RetainerHeapProfile BASE_EMBEDDED {
 public:
  class Printer {
   public:
    virtual ~Printer() {}
    virtual void PrintRetainers(const StringStream& retainers) = 0;
  };

  RetainerHeapProfile();
  void CollectStats(HeapObject* obj);
  void PrintStats();
  void DebugPrintStats(Printer* printer);
  void StoreReference(const JSObjectsCluster& cluster, Object* ref);

 private:
  JSObjectsCluster Clusterize(Object* obj);

  // Limit on the number of retainers to be printed per cluster.
  static const int MAX_RETAINERS_TO_PRINT = 50;
  ZoneScope zscope_;
  JSObjectsClusterTree retainers_tree_;
  ClustersCoarser coarser_;
  // TODO(mnaganov): Use some helper class to hold these state variables.
  JSObjectsClusterTree* coarse_cluster_tree_;
  int retainers_printed_;
  Printer* current_printer_;
  StringStream* current_stream_;
 public:
  // Used by JSObjectsClusterTree::ForEach.
  void Call(const JSObjectsCluster& cluster, JSObjectsClusterTree* tree);
};


#endif  // ENABLE_LOGGING_AND_PROFILING

} }  // namespace v8::internal

#endif  // V8_HEAP_PROFILER_H_
