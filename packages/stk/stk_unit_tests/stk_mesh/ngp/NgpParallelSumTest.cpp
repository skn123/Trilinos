#include <stk_ngp_test/ngp_test.hpp>
#include <stk_util/stk_config.h>
#include <stk_mesh/base/Ngp.hpp>
#include <stk_unit_test_utils/MeshFixture.hpp>
#include <stk_unit_test_utils/getOption.h>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Bucket.hpp>
#include <stk_mesh/base/Field.hpp>
#include <stk_mesh/base/Entity.hpp>
#include "stk_mesh/base/FieldParallel.hpp"
#include "stk_mesh/base/NgpFieldParallel.hpp"
#include "stk_mesh/base/GetNgpField.hpp"
#include "stk_mesh/base/GetNgpMesh.hpp"
#include <stk_util/environment/WallTime.hpp>
#include <stk_util/parallel/ParallelReduce.hpp>
#include <stk_util/parallel/DeviceAwareMPI.hpp>
#include "stk_unit_test_utils/TextMesh.hpp"
#include <stk_unit_test_utils/stk_mesh_fixtures/HexFixture.hpp>
#include "../UnitTestUtils.hpp"

namespace  {

template <typename T>
void check_field_on_device(stk::mesh::NgpMesh &mesh,
                           stk::mesh::NgpField<T> & userField,
                           stk::mesh::NgpField<T> & goldValues)
{
  stk::mesh::for_each_entity_run(mesh, stk::topology::NODE_RANK,
                                 mesh.get_bulk_on_host().mesh_meta_data().universal_part(),
                                 KOKKOS_LAMBDA(const stk::mesh::FastMeshIndex& entity)
                                 {
                                   NGP_EXPECT_NEAR(userField(entity, 0), goldValues(entity, 0), 1.e-12);
                                 });
}

class NgpParallelSum : public stk::unit_test_util::MeshFixture
{
protected:
  NgpParallelSum()
    : stk::unit_test_util::MeshFixture(3)
  {
  }

  void initialize_shared_values(stk::mesh::FieldBase & userField, stk::mesh::FieldBase & goldValues,
                                bool leaveGoldValuesNotSummed=false)
  {
    const stk::mesh::BucketVector & buckets = get_bulk().get_buckets(stk::topology::NODE_RANK, get_meta().globally_shared_part());
    std::vector<int> sharingProcs;
    for (stk::mesh::Bucket * bucket : buckets) {
      for (const stk::mesh::Entity & node : *bucket) {
        double id = static_cast<double>(get_bulk().identifier(node));
        double * gold = static_cast<double*>(stk::mesh::field_data(goldValues, node));
        *gold = id;

        double * user = static_cast<double*>(stk::mesh::field_data(userField, node));
        get_bulk().comm_procs(node, sharingProcs);
        const size_t numSharers = sharingProcs.size() + 1;
        *user = id / numSharers;
        if (leaveGoldValuesNotSummed) {
          *gold = *user;
        }
      }
    }
  }

  template <typename BUILD_MESH>
  void test_parallel_sum(BUILD_MESH buildMesh) {
    setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
    const int numStates = 1;
    stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
    stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
    stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
    stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

    buildMesh(get_bulk());

    initialize_shared_values(userField, goldValues);

    stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
    stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
    stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

    stk::mesh::parallel_sum<double>(get_bulk(), std::vector<stk::mesh::NgpField<double>*>{&deviceUserField});

    check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
  }
};

class NgpCopyOwnedToShared : public stk::unit_test_util::MeshFixture
{
protected:
  NgpCopyOwnedToShared()
    : stk::unit_test_util::MeshFixture(3)
  {
  }

  void initialize_owned_shared_values(stk::mesh::FieldBase & userField, stk::mesh::FieldBase & goldValues,
                                      bool leaveGoldValuesZero=false)
  {
    const stk::mesh::BucketVector & buckets = get_bulk().get_buckets(stk::topology::NODE_RANK, get_meta().globally_shared_part());
    for (stk::mesh::Bucket * bucket : buckets) {
      for (const stk::mesh::Entity & node : *bucket) {
        double id = static_cast<double>(get_bulk().identifier(node));
        double * gold = static_cast<double*>(stk::mesh::field_data(goldValues, node));
        *gold = id;

        double * user = static_cast<double*>(stk::mesh::field_data(userField, node));
        *user = bucket->owned() ? id : 0;
        if (leaveGoldValuesZero) {
          *gold = *user;
        }
      }
    }
  }

};

class NgpCommunicateFieldData : public stk::unit_test_util::MeshFixture
{
protected:
  NgpCommunicateFieldData()
    : stk::unit_test_util::MeshFixture(3)
  {
  }

  void initialize_owned_ghosted_values(stk::mesh::FieldBase & userField, stk::mesh::FieldBase & goldValues,
                                       bool leaveGoldValuesZero=false)
  {
    const stk::mesh::BucketVector & buckets = get_bulk().get_buckets(stk::topology::NODE_RANK, (get_meta().locally_owned_part() & !get_meta().globally_shared_part()) | get_meta().aura_part());
    for (stk::mesh::Bucket * bucket : buckets) {
      for (const stk::mesh::Entity & node : *bucket) {
        double id = static_cast<double>(get_bulk().identifier(node));
        double * gold = static_cast<double*>(stk::mesh::field_data(goldValues, node));
        *gold = id;

        double * user = static_cast<double*>(stk::mesh::field_data(userField, node));
        *user = bucket->owned() ? id : 0;
        if (leaveGoldValuesZero) {
          *gold = *user;
        }
      }
    }
  }

};

NGP_TEST_F(NgpParallelSum, simpleVersion)
{
  test_parallel_sum([](stk::mesh::BulkData& bulk){stk::io::fill_mesh("generated:1x1x4", bulk);});
}


void build_homogeneous_mesh_hex_proc_0(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,HEX_8,1,4,5,2,7,10,11,8\n"
                         "0,2,HEX_8,2,5,6,3,8,11,12,9\n";

  std::vector<double> coordinates {
    0,0,0, 0,1,0, 0,2,0,
    1,0,0, 1,1,0, 1,2,0,
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_homogeneous_mesh_hex_proc_1(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "1,1,HEX_8,1,4,5,2,7,10,11,8\n"
                         "1,2,HEX_8,2,5,6,3,8,11,12,9\n";

  std::vector<double> coordinates {
    0,0,0, 0,1,0, 0,2,0,
    1,0,0, 1,1,0, 1,2,0,
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_homogeneous_mesh_hex_both_procs(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,HEX_8,1,4,5,2,7,10,11,8\n"
                         "1,2,HEX_8,2,5,6,3,8,11,12,9\n";

  std::vector<double> coordinates {
    0,0,0, 0,1,0, 0,2,0,
    1,0,0, 1,1,0, 1,2,0,
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_homogeneous_mesh_tet_proc_0(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,TET_4,1,3,6,5\n"
                         "0,2,TET_4,2,6,4,7\n";

  std::vector<double> coordinates {
    0,0,1, 0,2,1,
    1,0,1, 1,2,1,
    0,0,2, 0,1,2, 0,2,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_homogeneous_mesh_tet_proc_1(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "1,1,TET_4,1,3,6,5\n"
                         "1,2,TET_4,2,6,4,7\n";

  std::vector<double> coordinates {
    0,0,1, 0,2,1,
    1,0,1, 1,2,1,
    0,0,2, 0,1,2, 0,2,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_homogeneous_mesh_tet_both_procs(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,TET_4,1,3,6,5\n"
                         "1,2,TET_4,2,6,4,7\n";

  std::vector<double> coordinates {
    0,0,1, 0,2,1,
    1,0,1, 1,2,1,
    0,0,2, 0,1,2, 0,2,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_homogeneous_mesh_pyramid_proc_0(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,PYRAMID_5,1,4,5,2,7\n"
                         "0,2,PYRAMID_5,2,5,6,3,7\n";

  std::vector<double> coordinates {
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1,
    0,1,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_homogeneous_mesh_pyramid_proc_1(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "1,1,PYRAMID_5,1,4,5,2,7\n"
                         "1,2,PYRAMID_5,2,5,6,3,7\n";

  std::vector<double> coordinates {
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1,
    0,1,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_homogeneous_mesh_pyramid_both_procs(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,PYRAMID_5,1,4,5,2,7\n"
                         "1,2,PYRAMID_5,2,5,6,3,7\n";

  std::vector<double> coordinates {
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1,
    0,1,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_hexProc0)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_hex_proc_0);
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_hexProc1)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_hex_proc_1);
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_hexBothProcs)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_hex_both_procs);
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_tetProc0)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_tet_proc_0);
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_tetProc1)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_tet_proc_1);
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_tetBothProcs)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_tet_both_procs);
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_pyramidProc0)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_pyramid_proc_0);
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_pyramidProc1)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_pyramid_proc_1);
}

NGP_TEST_F(NgpParallelSum, homogeneousMesh_pyramidBothProcs)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_homogeneous_mesh_pyramid_both_procs);
}


void build_heterogeneous_mesh_no_empty_blocks(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,HEX_8,1,4,5,2,7,10,11,8\n"
                         "1,2,HEX_8,2,5,6,3,8,11,12,9\n"
                         "0,3,PYRAMID_5,7,10,11,8,14\n"
                         "1,4,PYRAMID_5,8,11,12,9,14\n"
                         "0,5,TET_4,7,10,14,13\n"
                         "1,6,TET_4,9,14,12,15\n";

  std::vector<double> coordinates {
    0,0,0, 0,1,0, 0,2,0,
    1,0,0, 1,1,0, 1,2,0,
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1,
    0,0,2, 0,1,2, 0,2,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_heterogeneous_mesh_pyramid_only_on_proc_1(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,HEX_8,1,4,5,2,7,10,11,8\n"
                         "1,2,HEX_8,2,5,6,3,8,11,12,9\n"
                         "1,3,PYRAMID_5,7,10,11,8,14\n"
                         "1,4,PYRAMID_5,8,11,12,9,14\n"
                         "0,5,TET_4,7,10,14,13\n"
                         "1,6,TET_4,9,14,12,15\n";

  std::vector<double> coordinates {
    0,0,0, 0,1,0, 0,2,0,
    1,0,0, 1,1,0, 1,2,0,
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1,
    0,0,2, 0,1,2, 0,2,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}

void build_heterogeneous_mesh_elem_types_all_on_one_proc(stk::mesh::BulkData & bulk)
{
  std::string meshDesc = "0,1,HEX_8,1,4,5,2,7,10,11,8\n"
                         "0,2,HEX_8,2,5,6,3,8,11,12,9\n"
                         "1,3,PYRAMID_5,7,10,11,8,14\n"
                         "1,4,PYRAMID_5,8,11,12,9,14\n"
                         "1,5,TET_4,7,10,14,13\n"
                         "1,6,TET_4,9,14,12,15\n";

  std::vector<double> coordinates {
    0,0,0, 0,1,0, 0,2,0,
    1,0,0, 1,1,0, 1,2,0,
    0,0,1, 0,1,1, 0,2,1,
    1,0,1, 1,1,1, 1,2,1,
    0,0,2, 0,1,2, 0,2,2
  };

  stk::unit_test_util::setup_text_mesh(bulk, stk::unit_test_util::get_full_text_mesh_desc(meshDesc, coordinates));
}


NGP_TEST_F(NgpParallelSum, heterogeneousMesh_noEmptyBlocks)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_heterogeneous_mesh_no_empty_blocks);
}

NGP_TEST_F(NgpParallelSum, heterogeneousMesh_pyramidOnlyOnProc1)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_heterogeneous_mesh_pyramid_only_on_proc_1);
}

NGP_TEST_F(NgpParallelSum, heterogeneousMesh_elemTypesAllOnOneProc)
{
  if (get_parallel_size() != 2) return;

  test_parallel_sum(build_heterogeneous_mesh_elem_types_all_on_one_proc);
}

#ifdef STK_USE_DEVICE_MESH
NGP_TEST_F(NgpParallelSum, simpleVersion_noSyncToDeviceAfterwards)
{
  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh("generated:1x1x4", get_bulk());

  const bool leaveSharedGoldValuesNotSummed = true;
  initialize_shared_values(userField, goldValues, leaveSharedGoldValuesNotSummed);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  const bool finalSyncToDevice = false;
  stk::mesh::parallel_sum<double>(get_bulk(), std::vector<stk::mesh::NgpField<double>*>{&deviceUserField}, finalSyncToDevice);

  check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
}
#endif

NGP_TEST_F(NgpCopyOwnedToShared, simpleVersion)
{
  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh("generated:1x1x4", get_bulk());

  initialize_owned_shared_values(userField, goldValues);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  stk::mesh::copy_owned_to_shared<double>(get_bulk(), std::vector<stk::mesh::NgpField<double>*>{&deviceUserField});

  check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
}

#ifdef STK_USE_DEVICE_MESH
NGP_TEST_F(NgpCopyOwnedToShared, simpleVersion_noSyncToDeviceAfterwards)
{
  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh("generated:1x1x4", get_bulk());

  const bool leaveSharedGoldValuesZero = true;
  initialize_owned_shared_values(userField, goldValues, leaveSharedGoldValuesZero);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  const bool finalSyncToDevice = false;
  stk::mesh::copy_owned_to_shared<double>(get_bulk(), std::vector<stk::mesh::NgpField<double>*>{&deviceUserField}, finalSyncToDevice);

  check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
}
#endif

NGP_TEST_F(NgpCommunicateFieldData, simpleVersion_takesGhosting)
{
  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh("generated:1x1x4", get_bulk());

  initialize_owned_ghosted_values(userField, goldValues);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  stk::mesh::communicate_field_data<double>(*get_bulk().ghostings()[stk::mesh::BulkData::AURA], std::vector<stk::mesh::NgpField<double>*>{&deviceUserField});

  check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
}

#ifdef STK_USE_DEVICE_MESH
NGP_TEST_F(NgpCommunicateFieldData, simpleVersion_takesGhosting_noSyncToDeviceAfterwards)
{
  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh("generated:1x1x4", get_bulk());

  const bool leaveRecvGhostGoldValuesZero = true;
  initialize_owned_ghosted_values(userField, goldValues, leaveRecvGhostGoldValuesZero);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  const bool finalSyncToDevice = false;
  stk::mesh::communicate_field_data<double>(*get_bulk().ghostings()[stk::mesh::BulkData::AURA], std::vector<stk::mesh::NgpField<double>*>{&deviceUserField}, finalSyncToDevice);

  check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
}
#endif

NGP_TEST_F(NgpCommunicateFieldData, simpleVersion_takesBulkData)
{
  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh("generated:1x1x4", get_bulk());

  initialize_owned_ghosted_values(userField, goldValues);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  stk::mesh::communicate_field_data<double>(get_bulk(), std::vector<stk::mesh::NgpField<double>*>{&deviceUserField});

  check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
}

#ifdef STK_USE_DEVICE_MESH
NGP_TEST_F(NgpCommunicateFieldData, simpleVersion_takesBulkData_noSyncToDeviceAfterwards)
{
  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh("generated:1x1x4", get_bulk());

  const bool leaveRecvGhostGoldValuesZero = true;
  initialize_owned_ghosted_values(userField, goldValues, leaveRecvGhostGoldValuesZero);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  const bool finalSyncToDevice = false;
  stk::mesh::communicate_field_data<double>(get_bulk(), std::vector<stk::mesh::NgpField<double>*>{&deviceUserField}, finalSyncToDevice);

  check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
}

NGP_TEST_F(NgpParallelSum, DeviceMPIVersion)
{
  if (!stk::have_device_aware_mpi()) { GTEST_SKIP(); }

  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh("generated:1x1x4", get_bulk());

  initialize_shared_values(userField, goldValues);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  stk::mesh::parallel_sum(ngpMesh, std::vector<stk::mesh::NgpField<double>*>{&deviceUserField});

  check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
}
#endif

NGP_TEST_F(NgpParallelSum, Performance)
{
  if (!stk::have_device_aware_mpi()) { GTEST_SKIP(); }

  const std::string serialMeshName = "serialParallelSumMesh.g";
  if (stk::parallel_machine_rank(MPI_COMM_WORLD) == 0)
  {
    std::shared_ptr<stk::mesh::BulkData> bulk = stk::mesh::MeshBuilder(MPI_COMM_SELF).create();

    std::string meshSpecDefault = "10x10x10";
    std::string meshSpec = stk::unit_test_util::get_command_line_option("-m", meshSpecDefault);

    stk::io::fill_mesh("generated:" + meshSpec, *bulk);
    stk::io::write_mesh(serialMeshName, *bulk);
  }

  stk::parallel_machine_barrier(MPI_COMM_WORLD);

  setup_empty_mesh(stk::mesh::BulkData::NO_AUTO_AURA);
  const int numStates = 1;
  stk::mesh::Field<double> & userField  = get_meta().declare_field<double>(stk::topology::NODE_RANK, "userField", numStates);
  stk::mesh::Field<double> & goldValues = get_meta().declare_field<double>(stk::topology::NODE_RANK, "goldValues", numStates);
  stk::mesh::put_field_on_mesh(userField, get_meta().universal_part(), nullptr);
  stk::mesh::put_field_on_mesh(goldValues, get_meta().universal_part(), nullptr);

  stk::io::fill_mesh_with_auto_decomp(serialMeshName, get_bulk());

  initialize_shared_values(userField, goldValues);

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(get_bulk());
  stk::mesh::NgpField<double> & deviceUserField = stk::mesh::get_updated_ngp_field<double>(userField);
  stk::mesh::NgpField<double> & deviceGoldValues = stk::mesh::get_updated_ngp_field<double>(goldValues);

  const bool useSimpleDefault = true;
  bool useSimple = stk::unit_test_util::get_command_line_option("-s", useSimpleDefault);

  const int numIterationsDefault = 1;
  int numIterations = stk::unit_test_util::get_command_line_option("-n", numIterationsDefault);

  for (int i = 0; i < numIterations; ++i) {
    if (useSimple) {
      const double startTime = stk::wall_time();
      stk::mesh::parallel_sum<double>(get_bulk(), std::vector<stk::mesh::NgpField<double>*>{&deviceUserField});
      const double stopTime = stk::wall_time();
      const double localTime = stopTime - startTime;
      double globalTime = 0;
      stk::all_reduce_max(MPI_COMM_WORLD, &localTime, &globalTime, 1);

      if (get_bulk().parallel_rank() == 0) {
        std::cout << "Time for simple parallel_sum(): " << globalTime << " s" << std::endl;
      }
    }
    else {
      const double startTime = stk::wall_time();
      stk::mesh::parallel_sum(ngpMesh, std::vector<stk::mesh::NgpField<double>*>{&deviceUserField});
      const double stopTime = stk::wall_time();
      const double localTime = stopTime - startTime;
      double globalTime = 0;
      stk::all_reduce_max(MPI_COMM_WORLD, &localTime, &globalTime, 1);

      if (get_bulk().parallel_rank() == 0) {
        std::cout << "Time for NGP-aware parallel_sum(): " << globalTime << " s" << std::endl;
      }
    }
  }

  if (numIterations == 1) {
    check_field_on_device<double>(ngpMesh, deviceUserField, deviceGoldValues);
  }

  unlink(serialMeshName.c_str());
}

class NgpParallelOpIncludingGhosts : public ::ngp_testing::Test {
protected:
  NgpParallelOpIncludingGhosts() { }

  void setup_mesh(stk::mesh::BulkData::AutomaticAuraOption auraOption = stk::mesh::BulkData::NO_AUTO_AURA)
  {
    bulkPtr = stk::mesh::MeshBuilder(stk::parallel_machine_world())
                                .set_spatial_dimension(3)
                                .set_aura_option(auraOption)
                                .set_symmetric_ghost_info(true)
                                .create();
    stk::mesh::MetaData& meta = bulkPtr->mesh_meta_data();
    nodeField = &meta.declare_field<double>(stk::topology::NODE_RANK, "myNodeField");
    stk::mesh::put_field_on_mesh(*nodeField, meta.universal_part(), nullptr);
    elemField = &meta.declare_field<double>(stk::topology::ELEM_RANK, "myElemField");
    stk::mesh::put_field_on_mesh(*elemField, meta.universal_part(), nullptr);
    size_t nx=1, ny=1, nz=3;
    stk::mesh::fixtures::HexFixture::fill_mesh(nx,ny,nz, *bulkPtr);
  }

  void init_shared_field_values()
  {
    stk::mesh::Selector shared = bulkPtr->mesh_meta_data().globally_shared_part();
    const stk::mesh::BucketVector& sharedNodeBuckets = bulkPtr->get_buckets(stk::topology::NODE_RANK, shared);
    for(const stk::mesh::Bucket* bptr : sharedNodeBuckets) {
      for(stk::mesh::Entity node : *bptr) {
        double* value = stk::mesh::field_data(*nodeField, node);
        *value = bulkPtr->identifier(node);
      }
    }
  }

  void check_shared_field_values_on_host(stk::mesh::Operation op,
                                         stk::mesh::EntityId skipID = 0)
  {
    stk::mesh::Selector shared = bulkPtr->mesh_meta_data().globally_shared_part();
    std::vector<int> shProcs;
    constexpr double tol = std::numeric_limits<double>::epsilon();
    const stk::mesh::BucketVector& sharedNodeBuckets = bulkPtr->get_buckets(stk::topology::NODE_RANK, shared);
    for(const stk::mesh::Bucket* bptr : sharedNodeBuckets) {
      for(stk::mesh::Entity node : *bptr) {
        if (skipID != 0 && bulkPtr->identifier(node) == skipID) {
          continue;
        }
        bulkPtr->comm_shared_procs(node, shProcs);
        const double* value = stk::mesh::field_data(*nodeField, node);
        double expectedValue = (shProcs.size()+1) * bulkPtr->identifier(node);
        if (op == stk::mesh::Operation::MIN || op == stk::mesh::Operation::MAX) {
          expectedValue = bulkPtr->identifier(node);
        }
        EXPECT_NEAR(expectedValue, *value, tol)<<bulkPtr->entity_key(node);
      }
    }
  }

  std::shared_ptr<stk::mesh::BulkData> bulkPtr;
  stk::mesh::Field<double>* nodeField = nullptr;
  stk::mesh::Field<double>* elemField = nullptr;
};

NGP_TEST_F(NgpParallelOpIncludingGhosts, sum_hex_3procs_1ghostNode_host)
{
  stk::ParallelMachine comm = stk::parallel_machine_world();
  const int numProcs = stk::parallel_machine_size(comm);
  if(numProcs != 3) { GTEST_SKIP(); }
  const int myProc = stk::parallel_machine_rank(comm);

  setup_mesh();
  stk::mesh::unit_test::proc0_ghost_node_to_proc1_and_proc2(*bulkPtr, 1);
  init_shared_field_values();

  stk::mesh::Entity node1 = bulkPtr->get_entity(stk::topology::NODE_RANK, 1);
  EXPECT_TRUE(bulkPtr->is_valid(node1));

  double* value = stk::mesh::field_data(*nodeField, node1);
  const double initValue = (myProc+1);
  *value = initValue;

  stk::mesh::HostMesh hostNgpMesh(*bulkPtr);
  using HostNgpField = stk::mesh::HostField<double,stk::ngp::HostMemSpace>;
  HostNgpField hostNgpField(*bulkPtr,*nodeField);
  std::vector<HostNgpField*> hostNgpFields = {&hostNgpField};
  stk::mesh::parallel_sum_including_ghosts<stk::mesh::HostMesh,HostNgpField,stk::ngp::HostMemSpace>(hostNgpMesh, hostNgpFields);

  constexpr double tolerance = 1.e-9;

  double expectedValue = 0;
  for(int p=0; p<numProcs; ++p) {
    expectedValue += (p+1);
  }

  check_shared_field_values_on_host(stk::mesh::Operation::SUM);

  value = stk::mesh::field_data(*nodeField, node1);
  EXPECT_NEAR(*value, expectedValue, tolerance);
}

NGP_TEST_F(NgpParallelOpIncludingGhosts, sum_hex_3procs_node_and_elem_field)
{
  stk::ParallelMachine comm = stk::parallel_machine_world();
  const int numProcs = stk::parallel_machine_size(comm);
  if(numProcs != 3) { GTEST_SKIP(); }
  const int myProc = stk::parallel_machine_rank(comm);

  setup_mesh(stk::mesh::BulkData::AUTO_AURA);
  stk::mesh::unit_test::proc0_ghost_node_to_proc1_and_proc2(*bulkPtr, 1);
  init_shared_field_values();

  stk::mesh::Entity node1 = bulkPtr->get_entity(stk::topology::NODE_RANK, 1);
  EXPECT_TRUE(bulkPtr->is_valid(node1));

  double* value = stk::mesh::field_data(*nodeField, node1);
  const double initValue = (myProc+1);
  *value = initValue;

  stk::mesh::Entity elem1 = bulkPtr->get_entity(stk::topology::ELEM_RANK, 1);
  if (bulkPtr->is_valid(elem1)) {
    double* elemValue = stk::mesh::field_data(*elemField, elem1);
    const double initVal = 1.0;
    *elemValue = initVal;
  }

  stk::mesh::HostMesh hostNgpMesh(*bulkPtr);
  using HostNgpField = stk::mesh::HostField<double,stk::ngp::HostMemSpace>;
  HostNgpField hostNgpNodeField(*bulkPtr,*nodeField);
  HostNgpField hostNgpElemField(*bulkPtr,*elemField);
  std::vector<HostNgpField*> hostNgpFields = {&hostNgpNodeField,&hostNgpElemField};
  stk::mesh::parallel_sum_including_ghosts<stk::mesh::HostMesh,HostNgpField,stk::ngp::HostMemSpace>(hostNgpMesh, hostNgpFields);

  constexpr double tolerance = 1.e-9;

  double expectedValue = 0;
  for(int p=0; p<numProcs; ++p) {
    expectedValue += (p+1);
  }

  check_shared_field_values_on_host(stk::mesh::Operation::SUM);

  value = stk::mesh::field_data(*nodeField, node1);
  EXPECT_NEAR(*value, expectedValue, tolerance);

  elem1 = bulkPtr->get_entity(stk::topology::ELEM_RANK, 1);
  if (bulkPtr->is_valid(elem1)) {
    std::vector<int> commProcs;
    bulkPtr->comm_procs(elem1, commProcs);
    EXPECT_EQ(1u, commProcs.size());
    const double* elemValue = stk::mesh::field_data(*elemField, elem1);
    const double expectedVal = commProcs.size()+1.0;
    EXPECT_NEAR(*elemValue, expectedVal, 1.e-9);
  }
}

NGP_TEST_F(NgpParallelOpIncludingGhosts, max_hex_3procs_1ghostNode_host)
{
  stk::ParallelMachine comm = stk::parallel_machine_world();
  const int numProcs = stk::parallel_machine_size(comm);
  if(numProcs != 3) { GTEST_SKIP(); }
  const int myProc = stk::parallel_machine_rank(comm);

  setup_mesh();
  stk::mesh::unit_test::proc0_ghost_node_to_proc1_and_proc2(*bulkPtr, 1);
  init_shared_field_values();

  stk::mesh::Entity node1 = bulkPtr->get_entity(stk::topology::NODE_RANK, 1);
  EXPECT_TRUE(bulkPtr->is_valid(node1));

  double* value = stk::mesh::field_data(*nodeField, node1);
  const double initValue = (myProc+1);
  *value = initValue;

  stk::mesh::HostMesh hostNgpMesh(*bulkPtr);
  using HostNgpField = stk::mesh::HostField<double,stk::ngp::HostMemSpace>;
  HostNgpField hostNgpField(*bulkPtr,*nodeField);
  std::vector<HostNgpField*> hostNgpFields = {&hostNgpField};
  stk::mesh::parallel_max_including_ghosts<stk::mesh::HostMesh,HostNgpField,stk::ngp::HostMemSpace>(hostNgpMesh, hostNgpFields);

  constexpr double tolerance = 1.e-9;

  double expectedValue = numProcs;

  check_shared_field_values_on_host(stk::mesh::Operation::MAX);

  value = stk::mesh::field_data(*nodeField, node1);
  EXPECT_NEAR(*value, expectedValue, tolerance);
}

NGP_TEST_F(NgpParallelOpIncludingGhosts, min_hex_3procs_1ghostNode_host)
{
  stk::ParallelMachine comm = stk::parallel_machine_world();
  const int numProcs = stk::parallel_machine_size(comm);
  if(numProcs != 3) { GTEST_SKIP(); }
  const int myProc = stk::parallel_machine_rank(comm);

  setup_mesh();
  stk::mesh::unit_test::proc0_ghost_node_to_proc1_and_proc2(*bulkPtr, 1);
  init_shared_field_values();

  stk::mesh::Entity node1 = bulkPtr->get_entity(stk::topology::NODE_RANK, 1);
  EXPECT_TRUE(bulkPtr->is_valid(node1));

  double* value = stk::mesh::field_data(*nodeField, node1);
  const double initValue = (myProc+1);
  *value = initValue;

  stk::mesh::HostMesh hostNgpMesh(*bulkPtr);
  using HostNgpField = stk::mesh::HostField<double,stk::ngp::HostMemSpace>;
  HostNgpField hostNgpField(*bulkPtr,*nodeField);
  std::vector<HostNgpField*> hostNgpFields = {&hostNgpField};
  stk::mesh::parallel_min_including_ghosts<stk::mesh::HostMesh,HostNgpField,stk::ngp::HostMemSpace>(hostNgpMesh, hostNgpFields);

  constexpr double tolerance = 1.e-9;

  double expectedValue = 1;

  check_shared_field_values_on_host(stk::mesh::Operation::MAX);

  value = stk::mesh::field_data(*nodeField, node1);
  EXPECT_NEAR(*value, expectedValue, tolerance);
}

NGP_TEST_F(NgpParallelOpIncludingGhosts, sum_hex_3procs_1ghostNode_device)
{
  stk::ParallelMachine comm = stk::parallel_machine_world();
  const int numProcs = stk::parallel_machine_size(comm);
  if(numProcs != 3) { GTEST_SKIP(); }
#ifdef STK_ENABLE_GPU
  if (!stk::have_device_aware_mpi()) { GTEST_SKIP(); }
#endif
  const int myProc = stk::parallel_machine_rank(comm);

  setup_mesh();
  stk::mesh::unit_test::proc0_ghost_node_to_proc1_and_proc2(*bulkPtr, 1);
  init_shared_field_values();

  stk::mesh::Entity node1 = bulkPtr->get_entity(stk::topology::NODE_RANK, 1);
  EXPECT_TRUE(bulkPtr->is_valid(node1));

  double* value = stk::mesh::field_data(*nodeField, node1);
  const double initValue = (myProc+1);
  *value = initValue;

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(*bulkPtr);
  stk::mesh::NgpField<double> & ngpNodeField = stk::mesh::get_updated_ngp_field<double>(*nodeField);
  std::vector<stk::mesh::NgpField<double>*> ngpFields = {&ngpNodeField};
  stk::mesh::parallel_sum_including_ghosts(ngpMesh, ngpFields);

  nodeField->sync_to_host();

  constexpr double tolerance = 1.e-9;

  double expectedValue = 0;
  for(int p=0; p<numProcs; ++p) {
    expectedValue += (p+1);
  }

  check_shared_field_values_on_host(stk::mesh::Operation::SUM);

  value = stk::mesh::field_data(*nodeField, node1);
  EXPECT_NEAR(*value, expectedValue, tolerance);
}

NGP_TEST_F(NgpParallelOpIncludingGhosts, sum_hex_3procs_node5_sharedAndGhosted_device)
{
  stk::ParallelMachine comm = stk::parallel_machine_world();
  const int numProcs = stk::parallel_machine_size(comm);
  if(numProcs != 3) { GTEST_SKIP(); }
#ifdef STK_ENABLE_GPU
  if (!stk::have_device_aware_mpi()) { GTEST_SKIP(); }
#endif
  const int myProc = stk::parallel_machine_rank(comm);

  setup_mesh();
  stk::mesh::unit_test::proc0_ghost_node5_to_proc2(*bulkPtr);
  init_shared_field_values();

  stk::mesh::Entity node5 = bulkPtr->get_entity(stk::topology::NODE_RANK, 5);
  EXPECT_TRUE(bulkPtr->is_valid(node5));

  if (myProc == 2) {
    double* value = stk::mesh::field_data(*nodeField, node5);
    const double initValue = 5;
    *value = initValue;
  }

  stk::mesh::NgpMesh & ngpMesh = stk::mesh::get_updated_ngp_mesh(*bulkPtr);
  stk::mesh::NgpField<double> & ngpNodeField = stk::mesh::get_updated_ngp_field<double>(*nodeField);
  std::vector<stk::mesh::NgpField<double>*> ngpFields = {&ngpNodeField};
  stk::mesh::parallel_sum_including_ghosts(ngpMesh, ngpFields);

  nodeField->sync_to_host();

  stk::mesh::EntityId nodeToSkip = 5;
  check_shared_field_values_on_host(stk::mesh::Operation::SUM,nodeToSkip);

  constexpr double tolerance = 1.e-9;

  double expectedValue = numProcs*5;

  double* value = stk::mesh::field_data(*nodeField, node5);
  EXPECT_NEAR(*value, expectedValue, tolerance);
}

}
