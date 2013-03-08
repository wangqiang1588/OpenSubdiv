//
//     Copyright (C) Pixar. All rights reserved.
//
//     This license governs use of the accompanying software. If you
//     use the software, you accept this license. If you do not accept
//     the license, do not use the software.
//
//     1. Definitions
//     The terms "reproduce," "reproduction," "derivative works," and
//     "distribution" have the same meaning here as under U.S.
//     copyright law.  A "contribution" is the original software, or
//     any additions or changes to the software.
//     A "contributor" is any person or entity that distributes its
//     contribution under this license.
//     "Licensed patents" are a contributor's patent claims that read
//     directly on its contribution.
//
//     2. Grant of Rights
//     (A) Copyright Grant- Subject to the terms of this license,
//     including the license conditions and limitations in section 3,
//     each contributor grants you a non-exclusive, worldwide,
//     royalty-free copyright license to reproduce its contribution,
//     prepare derivative works of its contribution, and distribute
//     its contribution or any derivative works that you create.
//     (B) Patent Grant- Subject to the terms of this license,
//     including the license conditions and limitations in section 3,
//     each contributor grants you a non-exclusive, worldwide,
//     royalty-free license under its licensed patents to make, have
//     made, use, sell, offer for sale, import, and/or otherwise
//     dispose of its contribution in the software or derivative works
//     of the contribution in the software.
//
//     3. Conditions and Limitations
//     (A) No Trademark License- This license does not grant you
//     rights to use any contributor's name, logo, or trademarks.
//     (B) If you bring a patent claim against any contributor over
//     patents that you claim are infringed by the software, your
//     patent license from such contributor to the software ends
//     automatically.
//     (C) If you distribute any portion of the software, you must
//     retain all copyright, patent, trademark, and attribution
//     notices that are present in the software.
//     (D) If you distribute any portion of the software in source
//     code form, you may do so only under this license by including a
//     complete copy of this license with your distribution. If you
//     distribute any portion of the software in compiled or object
//     code form, you may only do so under a license that complies
//     with this license.
//     (E) The software is licensed "as-is." You bear the risk of
//     using it. The contributors give no express warranties,
//     guarantees or conditions. You may have additional consumer
//     rights under your local laws which this license cannot change.
//     To the extent permitted under your local laws, the contributors
//     exclude the implied warranties of merchantability, fitness for
//     a particular purpose and non-infringement.
//

#ifndef FAR_MULTI_MESH_FACTORY_H
#define FAR_MULTI_MESH_FACTORY_H

#include "../version.h"

#include "../far/mesh.h"
#include "../far/bilinearSubdivisionTablesFactory.h"
#include "../far/catmarkSubdivisionTablesFactory.h"
#include "../far/loopSubdivisionTablesFactory.h"
#include "../far/patchTablesFactory.h"
#include "../far/vertexEditTablesFactory.h"

#include <typeinfo>

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

template <class T, class U=T> class FarMultiMeshFactory {
    typedef std::vector<FarMesh<U> const *> FarMeshVector;

public:
    FarMultiMeshFactory() {}
    FarMesh<U> * Create(std::vector<FarMesh<U> const *> const &meshes);

private:
    // splice subdivision tables
    FarSubdivisionTables<U> * spliceSubdivisionTables(FarMesh<U> *farmesh, FarMeshVector const &meshes);

    // splice patch tables
    FarPatchTables * splicePatchTables(FarMeshVector const &meshes);

    // splice quad indices
    void spliceQuads(FarMesh<U> *result, FarMeshVector const &meshes);

    // splice hierarchical edit tables
    FarVertexEditTables<U> * spliceVertexEditTables(FarMesh<U> *farmesh, FarMeshVector const &meshes);

    int _maxlevel;
    int _maxvalence;
};

template <class T, class U> FarMesh<U> *
FarMultiMeshFactory<T, U>::Create(std::vector<FarMesh<U> const *> const &meshes) {

    if (meshes.empty()) return NULL;

    bool adaptive = (meshes[0]->GetPatchTables() != NULL);
    const std::type_info &scheme = typeid(*(meshes[0]->GetSubdivisionTables()));
    _maxlevel = 0;
    _maxvalence = 0;

    for (size_t i = 0; i < meshes.size(); ++i) {
        FarMesh<U> const *mesh = meshes[i];
        // XXX: once uniform quads are integrated into patch tables,
        // this restriction can be relaxed so that we can merge adaptive and uniform meshes together.
        if (adaptive ^ (mesh->GetPatchTables() != NULL)) {
            assert(false);
            return NULL;
        }

        // meshes have to have a same subdivision scheme
        if (scheme != typeid(*(mesh->GetSubdivisionTables()))) {
            assert(false);
            return NULL;
        }

        _maxlevel = std::max(_maxlevel, mesh->GetSubdivisionTables()->GetMaxLevel()-1);
        if (mesh->GetPatchTables()) {
            _maxvalence = std::max(_maxvalence, mesh->GetPatchTables()->GetMaxValence());
        }
    }

    FarMesh<U> * result = new FarMesh<U>();

    // splice subdivision tables
    result->_subdivisionTables = spliceSubdivisionTables(result, meshes);

    // splice patch/quad index tables
    if ( adaptive ) {
        result->_patchTables = splicePatchTables(meshes);
    } else {
        spliceQuads(result, meshes);
    }

    // splice vertex edit tables
    result->_vertexEditTables = spliceVertexEditTables(result, meshes);

    // count total num vertices
    int numVertices = 0;
    for (size_t i = 0; i < meshes.size(); ++i) {
        numVertices += meshes[i]->GetNumVertices();
    }
    result->_vertices.resize(numVertices);

    return result;
}

template <typename V, typename IT> static IT
copyWithOffset(IT dst_iterator, V const &src, int offset) {
    return std::transform(src.begin(), src.end(), dst_iterator,
                          std::bind2nd(std::plus<typename V::value_type>(), offset));
}

template <typename V, typename IT> static IT
copyWithOffsetF_ITa(IT dst_iterator, V const &src, int offset) {
    for (typename V::const_iterator it = src.begin(); it != src.end();) {
        *dst_iterator++ = *it++ + offset;   // offset to F_IT
        *dst_iterator++ = *it++;            // valence
    }
    return dst_iterator;
}

template <typename V, typename IT> static IT
copyWithOffsetE_IT(IT dst_iterator, V const &src, int offset) {
    for (typename V::const_iterator it = src.begin(); it != src.end(); ++it) {
        *dst_iterator++ = (*it == -1) ? -1 : (*it + offset);
    }
    return dst_iterator;
}

template <typename V, typename IT> static IT
copyWithOffsetV_ITa(IT dst_iterator, V const &src, int tableOffset, int vertexOffset) {
    for (typename V::const_iterator it = src.begin(); it != src.end();) {
        *dst_iterator++ = *it++ + tableOffset;   // offset to V_IT
        *dst_iterator++ = *it++;                 // valence
        *dst_iterator++ = (*it == -1) ? -1 : (*it + vertexOffset); ++it;
        *dst_iterator++ = (*it == -1) ? -1 : (*it + vertexOffset); ++it;
        *dst_iterator++ = (*it == -1) ? -1 : (*it + vertexOffset); ++it;
    }
    return dst_iterator;
}

template <typename V, typename IT> static IT
copyWithOffsetVertexValence(IT dst_iterator, V const &src, int srcMaxValence, int dstMaxValence, int offset) {
    for (typename V::const_iterator it = src.begin(); it != src.end(); ) {
        int valence = *it++;
        *dst_iterator++ = valence;
        valence = abs(valence);
        for (int i = 0; i < 2*dstMaxValence; ++i) {
            if (i < 2*srcMaxValence) {
                *dst_iterator++ = (i < 2*valence) ? *it + offset : 0;
                ++it;
            } else {
                *dst_iterator++ = 0;
            }
        }
    }
    return dst_iterator;
}

template <class T, class U> FarSubdivisionTables<U> *
FarMultiMeshFactory<T, U>::spliceSubdivisionTables(FarMesh<U> *farMesh, FarMeshVector const &meshes) {

    // count total table size
    size_t total_F_ITa = 0, total_F_IT = 0;
    size_t total_E_IT = 0, total_E_W = 0;
    size_t total_V_ITa = 0, total_V_IT = 0, total_V_W = 0;
    for (size_t i = 0; i < meshes.size(); ++i) {
        FarSubdivisionTables<U> const * tables = meshes[i]->GetSubdivisionTables();
        assert(tables);

        total_F_ITa += tables->Get_F_ITa().size();
        total_F_IT  += tables->Get_F_IT().size();
        total_E_IT  += tables->Get_E_IT().size();
        total_E_W   += tables->Get_E_W().size();
        total_V_ITa += tables->Get_V_ITa().size();
        total_V_IT  += tables->Get_V_IT().size();
        total_V_W   += tables->Get_V_W().size();
    }

    FarSubdivisionTables<U> *result = NULL;
    const std::type_info &scheme = typeid(*(meshes[0]->GetSubdivisionTables()));

    if (scheme == typeid(FarCatmarkSubdivisionTables<U>) ) {
        result = new FarCatmarkSubdivisionTables<U>(farMesh, _maxlevel);
    } else if (scheme == typeid(FarBilinearSubdivisionTables<U>) ) {
        result = new FarBilinearSubdivisionTables<U>(farMesh, _maxlevel);
    } else if (scheme == typeid(FarLoopSubdivisionTables<U>) ) {
        result = new FarLoopSubdivisionTables<U>(farMesh, _maxlevel);
    }

    result->_F_ITa.resize(total_F_ITa);
    result->_F_IT.resize(total_F_IT);
    result->_E_IT.resize(total_E_IT);
    result->_E_W.resize(total_E_W);
    result->_V_ITa.resize(total_V_ITa);
    result->_V_IT.resize(total_V_IT);
    result->_V_W.resize(total_V_W);

    // compute table offsets;
    std::vector<int> vertexOffsets;
    std::vector<int> fvOffsets;
    std::vector<int> evOffsets;
    std::vector<int> vvOffsets;
    std::vector<int> F_IToffsets;
    std::vector<int> V_IToffsets;

    {
        int vertexOffset = 0;
        int F_IToffset = 0;
        int V_IToffset = 0;
        int fvOffset = 0;
        int evOffset = 0;
        int vvOffset = 0;
        for (size_t i = 0; i < meshes.size(); ++i) {
            FarSubdivisionTables<U> const * tables = meshes[i]->GetSubdivisionTables();
            assert(tables);
            
            vertexOffsets.push_back(vertexOffset);
            F_IToffsets.push_back(F_IToffset);
            V_IToffsets.push_back(V_IToffset);
            fvOffsets.push_back(fvOffset);
            evOffsets.push_back(evOffset);
            vvOffsets.push_back(vvOffset);

            vertexOffset += meshes[i]->GetNumVertices();
            F_IToffset += (int)tables->Get_F_IT().size();
            fvOffset += (int)tables->Get_F_ITa().size()/2;
            V_IToffset += (int)tables->Get_V_IT().size();

            if (scheme == typeid(FarCatmarkSubdivisionTables<U>) ||
                scheme == typeid(FarLoopSubdivisionTables<U>)) {
                evOffset += (int)tables->Get_E_IT().size()/4;
                vvOffset += (int)tables->Get_V_ITa().size()/5;
            } else {
                evOffset += (int)tables->Get_E_IT().size()/2;
                vvOffset += (int)tables->Get_V_ITa().size();
            }
        }
    }

    // concat F_IT and V_IT
    std::vector<unsigned int>::iterator F_IT = result->_F_IT.begin();
    std::vector<unsigned int>::iterator V_IT = result->_V_IT.begin();

    for (size_t i = 0; i < meshes.size(); ++i) {
        FarSubdivisionTables<U> const * tables = meshes[i]->GetSubdivisionTables();

        int vertexOffset = vertexOffsets[i];
        // remap F_IT, V_IT tables
        F_IT = copyWithOffset(F_IT, tables->Get_F_IT(), vertexOffset);
        V_IT = copyWithOffset(V_IT, tables->Get_V_IT(), vertexOffset);
    }

    // merge other tables
    std::vector<int>::iterator F_ITa = result->_F_ITa.begin();
    std::vector<int>::iterator E_IT  = result->_E_IT.begin();
    std::vector<float>::iterator E_W = result->_E_W.begin();
    std::vector<float>::iterator V_W = result->_V_W.begin();
    std::vector<int>::iterator V_ITa = result->_V_ITa.begin();

    for (size_t i = 0; i < meshes.size(); ++i) {
        FarSubdivisionTables<U> const * tables = meshes[i]->GetSubdivisionTables();

        // copy face tables
        F_ITa = copyWithOffsetF_ITa(F_ITa, tables->Get_F_ITa(), F_IToffsets[i]);

        // copy edge tables
        E_IT = copyWithOffsetE_IT(E_IT, tables->Get_E_IT(), vertexOffsets[i]);
        E_W = copyWithOffset(E_W, tables->Get_E_W(), 0);

        // copy vert tables
        if (scheme == typeid(FarCatmarkSubdivisionTables<U>) ||
            scheme == typeid(FarLoopSubdivisionTables<U>)) {
            V_ITa = copyWithOffsetV_ITa(V_ITa, tables->Get_V_ITa(), V_IToffsets[i], vertexOffsets[i]);
        } else {
            V_ITa = copyWithOffset(V_ITa, tables->Get_V_ITa(), vertexOffsets[i]);
        }
        V_W = copyWithOffset(V_W, tables->Get_V_W(), 0);
    }

    // merge batch, model by model
    FarKernelBatchVector &batches = farMesh->_batches;
    int editTableIndexOffset = 0;
    for (size_t i = 0; i < meshes.size(); ++i) {
        for (int j = 0; j < (int)meshes[i]->_batches.size(); ++j) {
            FarKernelBatch batch = meshes[i]->_batches[j];
            batch.vertexOffset += vertexOffsets[i];
            
            if (batch.kernelType == CATMARK_FACE_VERTEX or
                batch.kernelType == BILINEAR_FACE_VERTEX) {
                batch.tableOffset += fvOffsets[i];
            } else if (batch.kernelType == CATMARK_EDGE_VERTEX or
                       batch.kernelType == LOOP_EDGE_VERTEX or
                       batch.kernelType == BILINEAR_EDGE_VERTEX) {
                batch.tableOffset += evOffsets[i];
            } else if (batch.kernelType == CATMARK_VERT_VERTEX_A1 or
                       batch.kernelType == CATMARK_VERT_VERTEX_A2 or
                       batch.kernelType == CATMARK_VERT_VERTEX_B or
                       batch.kernelType == LOOP_VERT_VERTEX_A1 or
                       batch.kernelType == LOOP_VERT_VERTEX_A2 or
                       batch.kernelType == LOOP_VERT_VERTEX_B or
                       batch.kernelType == BILINEAR_VERT_VERTEX) {
                batch.tableOffset += vvOffsets[i];
            } else if (batch.kernelType == HIERARCHICAL_EDIT) {
                batch.tableIndex += editTableIndexOffset;
            }
            batches.push_back(batch);
        }
        editTableIndexOffset += meshes[i]->_vertexEditTables ? meshes[i]->_vertexEditTables->GetNumBatches() : 0;
    }
    return result;
}        

template <class T, class U> void
FarMultiMeshFactory<T, U>::spliceQuads(FarMesh<U> *result, FarMeshVector const &meshes) {

    result->_faceverts.clear();
    result->_faceverts.resize(_maxlevel+1);

    // apply vertex offset and concatenate quad indices
    for (int l = 0; l <= _maxlevel; ++l) {
        int vertexOffset = 0;
        for (size_t i = 0; i < meshes.size(); ++i) {
            copyWithOffset(std::back_inserter(result->_faceverts[l]),
                           meshes[i]->_faceverts[l], vertexOffset);
            vertexOffset += meshes[i]->GetNumVertices();
        }
    }
}

template <class T, class U> FarPatchTables *
FarMultiMeshFactory<T, U>::splicePatchTables(FarMeshVector const &meshes) {

    FarPatchTables *result = new FarPatchTables(_maxvalence);

    int total_quadOffset0 = 0;
    int total_quadOffset1 = 0;

    std::vector<int> vertexOffsets;
    int vertexOffset = 0;
    int maxValence = 0;

    result->_patchCounts.reserve(meshes.size());
    FarPatchCount totalCount;

    // count how many patches exist on each mesh
    for (size_t i = 0; i < meshes.size(); ++i) {
        const FarPatchTables *ptables = meshes[i]->GetPatchTables();
        assert(ptables);
        
        vertexOffsets.push_back(vertexOffset);
        vertexOffset += meshes[i]->GetNumVertices();

        // accum patch counts. assuming given patch table has one element
        const FarPatchCount &patchCount = ptables->GetPatchCounts()[0];
        result->_patchCounts.push_back(patchCount);
        totalCount.Append(patchCount);

        // need to align maxvalence with the highest value
        maxValence = std::max(maxValence, ptables->_maxValence);
        total_quadOffset0 += (int)ptables->_full._G_IT.first.size();
        total_quadOffset1 += (int)ptables->_full._G_B_IT.first.size();
    }

    // Allocate full patches
    result->_full._R_IT.first.resize(totalCount.regular*16);
    result->_full._R_IT.second.resize(totalCount.regular);
    result->_full._B_IT.first.resize(totalCount.boundary*12);
    result->_full._B_IT.second.resize(totalCount.boundary);
    result->_full._C_IT.first.resize(totalCount.corner*9);
    result->_full._C_IT.second.resize(totalCount.corner);
    result->_full._G_IT.first.resize(totalCount.gregory*4);
    result->_full._G_IT.second.resize(totalCount.gregory);
    result->_full._G_B_IT.first.resize(totalCount.boundaryGregory*4);
    result->_full._G_B_IT.second.resize(totalCount.boundaryGregory);

    // Allocate transition Patches
    for (int i=0; i<5; ++i) {
        result->_transition[i]._R_IT.first.resize(totalCount.transitionRegular[i]*16);
        result->_transition[i]._R_IT.second.resize(totalCount.transitionRegular[i]);
        for (int j=0; j<4; ++j) {
            result->_transition[i]._B_IT[j].first.resize(totalCount.transitionBoundary[i][j]*12);
            result->_transition[i]._B_IT[j].second.resize(totalCount.transitionBoundary[i][j]);
            result->_transition[i]._C_IT[j].first.resize(totalCount.transitionCorner[i][j]*9);
            result->_transition[i]._C_IT[j].second.resize(totalCount.transitionCorner[i][j]);
        }
    }
    // Allocate vertex valence table, quad offset table
    if ((result->_full._G_IT.first.size() + result->_full._G_B_IT.first.size()) > 0) {
        result->_vertexValenceTable.resize((2*maxValence+1) * vertexOffset);
        result->_quadOffsetTable.resize(total_quadOffset0 + total_quadOffset1);
    }

    typedef struct IndexIterators {
        std::vector<unsigned int>::iterator R_P, B_P[4], C_P[4], G_P[2];
    } IndexIterator;

    typedef struct LevelIterators {
        std::vector<unsigned char>::iterator R_P, B_P[4], C_P[4], G_P[2];
    } LevelIterator;
    IndexIterator full, transition[5];
    LevelIterator fullLv, transitionLv[5];

    // prepare destination iterators
    full.R_P = result->_full._R_IT.first.begin();
    full.B_P[0] = result->_full._B_IT.first.begin();
    full.C_P[0] = result->_full._C_IT.first.begin();
    full.G_P[0] = result->_full._G_IT.first.begin();
    full.G_P[1] = result->_full._G_B_IT.first.begin();
    for (int i = 0; i < 5; ++i) {
        transition[i].R_P = result->_transition[i]._R_IT.first.begin();
        for (int j = 0 ; j < 4; ++j) {
            transition[i].B_P[j] = result->_transition[i]._B_IT[j].first.begin();
            transition[i].C_P[j] = result->_transition[i]._C_IT[j].first.begin();
        }
    }
    fullLv.R_P = result->_full._R_IT.second.begin();
    fullLv.B_P[0] = result->_full._B_IT.second.begin();
    fullLv.C_P[0] = result->_full._C_IT.second.begin();
    fullLv.G_P[0] = result->_full._G_IT.second.begin();
    fullLv.G_P[1] = result->_full._G_B_IT.second.begin();
    for (int i = 0; i < 5; ++i) {
        transitionLv[i].R_P = result->_transition[i]._R_IT.second.begin();
        for (int j = 0 ; j < 4; ++j) {
            transitionLv[i].B_P[j] = result->_transition[i]._B_IT[j].second.begin();
            transitionLv[i].C_P[j] = result->_transition[i]._C_IT[j].second.begin();
        }
    }

    // merge tables with vertex index offset
    for (size_t i = 0; i < meshes.size(); ++i) {
        const FarPatchTables *ptables = meshes[i]->GetPatchTables();
        int vertexOffset = vertexOffsets[i];

        full.R_P    = copyWithOffset(full.R_P,    ptables->_full._R_IT.first, vertexOffset);
        full.B_P[0] = copyWithOffset(full.B_P[0], ptables->_full._B_IT.first, vertexOffset);
        full.C_P[0] = copyWithOffset(full.C_P[0], ptables->_full._C_IT.first, vertexOffset);
        full.G_P[0] = copyWithOffset(full.G_P[0], ptables->_full._G_IT.first, vertexOffset);
        full.G_P[1] = copyWithOffset(full.G_P[1], ptables->_full._G_B_IT.first, vertexOffset);

        fullLv.R_P    = copyWithOffset(fullLv.R_P,    ptables->_full._R_IT.second, 0);
        fullLv.B_P[0] = copyWithOffset(fullLv.B_P[0], ptables->_full._B_IT.second, 0);
        fullLv.C_P[0] = copyWithOffset(fullLv.C_P[0], ptables->_full._C_IT.second, 0);
        fullLv.G_P[0] = copyWithOffset(fullLv.G_P[0], ptables->_full._G_IT.second, 0);
        fullLv.G_P[1] = copyWithOffset(fullLv.G_P[1], ptables->_full._G_B_IT.second, 0);

        for (int t = 0; t < 5; ++t) {
            transition[t].R_P = copyWithOffset(transition[t].R_P, ptables->_transition[t]._R_IT.first, vertexOffset);
            transitionLv[t].R_P = copyWithOffset(transitionLv[t].R_P, ptables->_transition[t]._R_IT.second, 0);

            for (int r = 0; r < 4; ++r) {
                transition[t].B_P[r] = copyWithOffset(transition[t].B_P[r], ptables->_transition[t]._B_IT[r].first, vertexOffset);
                transition[t].C_P[r] = copyWithOffset(transition[t].C_P[r], ptables->_transition[t]._C_IT[r].first, vertexOffset);
                transitionLv[t].B_P[r] = copyWithOffset(transitionLv[t].B_P[r], ptables->_transition[t]._B_IT[r].second, 0);
                transitionLv[t].C_P[r] = copyWithOffset(transitionLv[t].C_P[r], ptables->_transition[t]._C_IT[r].second, 0);
            }
        }
    }

    // merge vertexvalence and quadoffset tables
    std::vector<unsigned int>::iterator Q0_IT = result->_quadOffsetTable.begin();
    std::vector<unsigned int>::iterator Q1_IT = Q0_IT + total_quadOffset0;

    std::vector<int>::iterator VV_IT = result->_vertexValenceTable.begin();
    for (size_t i = 0; i < meshes.size(); ++i) {
        const FarPatchTables *ptables = meshes[i]->GetPatchTables();

        // merge vertex valence
        // note: some prims may not have vertex valence table, but still need a space
        // in order to fill following prim's data at appropriate location.
        copyWithOffsetVertexValence(VV_IT, ptables->_vertexValenceTable, ptables->_maxValence, maxValence, vertexOffsets[i]);
        VV_IT += meshes[i]->GetNumVertices() * (2 * maxValence + 1);

        // merge quad offsets
        int nGregoryQuads = (int)ptables->_full._G_IT.first.size();
        Q0_IT = std::copy(ptables->_quadOffsetTable.begin(),
                          ptables->_quadOffsetTable.begin()+nGregoryQuads,
                          Q0_IT);
        Q1_IT = std::copy(ptables->_quadOffsetTable.begin()+nGregoryQuads,
                          ptables->_quadOffsetTable.end(),
                          Q1_IT);
    }

    return result;
}

template <class T, class U> FarVertexEditTables<U> *
FarMultiMeshFactory<T, U>::spliceVertexEditTables(FarMesh<U> *farMesh, FarMeshVector const &meshes) {

    FarVertexEditTables<U> * result = new FarVertexEditTables<U>(farMesh);

    // at this moment, don't merge vertex edit tables (separate batch)
    for (size_t i = 0; i < meshes.size(); ++i) {
        const FarVertexEditTables<U> *vertexEditTables = meshes[i]->GetVertexEdit();
        if (not vertexEditTables) continue;

        // copy each edit batch  XXX:inefficient copy
        result->_batches.insert(result->_batches.end(),
                                vertexEditTables->_batches.begin(),
                                vertexEditTables->_batches.end());
    }

    if (result->_batches.empty()) {
        delete result;
        return NULL;
    }
    return result;
}

} // end namespace OPENSUBDIV_VERSION
using namespace OPENSUBDIV_VERSION;

} // end namespace OpenSubdiv

#endif /* FAR_MULTI_MESH_FACTORY_H */
