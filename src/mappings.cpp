#include "mappings.hpp"
#include "utils.hpp"

#include "seqindex.hpp"

#include "btllib/data_stream.hpp"
#include "btllib/status.hpp"
#include "btllib/util.hpp"

#include <fstream>
#include <string>

const std::vector<SeqId> AllMappings::EMPTY_MAPPINGS;

AllMappings::AllMappings(const std::string& filepath,
                         const SeqIndex& target_seqs_index,
                         unsigned mx_threshold_min,
                         unsigned mx_threshold_max,
                         double mx_max_mapped_seqs_per_target_10kbp)
{
  if (btllib::endswith(filepath, ".sam") ||
      btllib::endswith(filepath, ".bam")) {
    load_sam(filepath, target_seqs_index);
  } else if (btllib::endswith(filepath, ".paf")) {
    load_paf(filepath, target_seqs_index);
  } else {
    load_ntlink(filepath, target_seqs_index, mx_threshold_min);
    filter(mx_max_mapped_seqs_per_target_10kbp,
           mx_threshold_min,
           mx_threshold_max,
           target_seqs_index);
  }
  all_mx_in_common.clear();
  all_inserted_mappings.clear();
}

void
AllMappings::load_mapping(const std::string& mapped_seq_id,
                          const std::string& target_seq_id,
                          const SeqIndex& target_seqs_index,
                          const unsigned mx)
{
  if (target_seqs_index.seq_exists(target_seq_id)) {
    decltype(all_mappings)::iterator it_all_mappings;
    decltype(all_inserted_mappings)::iterator it_all_inserted_mappings;
    decltype(all_mx_in_common)::iterator it_all_mx_in_common;

    it_all_mappings = all_mappings.find(target_seq_id);
    if (it_all_mappings == all_mappings.end()) {
      const auto emplacement_all_mappings = all_mappings.emplace(
        target_seq_id, decltype(all_mappings)::mapped_type());
      const auto emplacement_all_inserted_mappings =
        all_inserted_mappings.emplace(
          target_seq_id, decltype(all_inserted_mappings)::mapped_type());
      const auto emplacement_all_mx_in_common = all_mx_in_common.emplace(
        target_seq_id, decltype(all_mx_in_common)::mapped_type());
      it_all_mappings = emplacement_all_mappings.first;
      it_all_inserted_mappings = emplacement_all_inserted_mappings.first;
      it_all_mx_in_common = emplacement_all_mx_in_common.first;
    } else {
      it_all_inserted_mappings = all_inserted_mappings.find(target_seq_id);
      it_all_mx_in_common = all_mx_in_common.find(target_seq_id);
    }

    if (it_all_inserted_mappings->second.find(mapped_seq_id) ==
        it_all_inserted_mappings->second.end()) {
      it_all_mappings->second.emplace_back(mapped_seq_id);
      it_all_inserted_mappings->second.emplace(mapped_seq_id);
      it_all_mx_in_common->second.emplace_back(mx);
    }
  }
}

void
AllMappings::load_ntlink(const std::string& filepath,
                         const SeqIndex& target_seqs_index,
                         const unsigned mx_threshold_min)
{
  btllib::log_info(FN_NAME + ": Loading ntLink mappings from " + filepath +
                   "... ");

  std::ifstream ifs(filepath);
  btllib::check_stream(ifs, filepath);
  std::string token, mapped_seq_id, target_seq_id;
  unsigned long i = 0;
  while (bool(ifs >> token)) {
    switch (i % 3) {
      case 0:
        mapped_seq_id = std::move(token);
        break;
      case 1:
        target_seq_id = std::move(token);
        break;
      case 2: {
        const auto minimizers = std::stoul(token);
        if (minimizers >= mx_threshold_min) {
          load_mapping(
            mapped_seq_id, target_seq_id, target_seqs_index, minimizers);
        }
        break;
      }
      default: {
        btllib::log_error(FN_NAME + ": Invalid switch branch.");
        std::exit(EXIT_FAILURE); // NOLINT(concurrency-mt-unsafe)
      }
    }
    i++;
  }
  btllib::log_info(FN_NAME + ": Done!");
}

void
AllMappings::load_sam(const std::string& filepath,
                      const SeqIndex& target_seqs_index)
{
  enum Column
  {
    QNAME = 1,
    FLAG,
    RNAME,
    POS,
    MAPQ,
    CIGAR,
    RNEXT,
    PNEXT,
    TLEN,
    SEQ,
    QUAL
  };

  btllib::log_info(FN_NAME + ": Loading SAM mappings from " + filepath +
                   "... ");

  size_t n = 2048; // NOLINT
  char* line = new char[n];
  btllib::DataSource data_source(filepath);

  std::string token, mapped_seq_id, target_seq_id;
  while (getline(&line, &n, data_source) > 0) {
    if (line[0] == '@') {
      continue;
    }
    std::stringstream ss(line);
    Column column = QNAME;
    while (bool(ss >> token)) {
      switch (column) {
        case QNAME:
          mapped_seq_id = std::move(token);
          break;
        case RNAME:
          target_seq_id = std::move(token);
          break;
        default: {
          break;
        }
      }
      column = Column(int(column) + 1);
    }
    load_mapping(mapped_seq_id, target_seq_id, target_seqs_index, 0);
  }
  btllib::log_info(FN_NAME + ": Done!");
}

void
AllMappings::load_paf(const std::string& filepath,
                      const SeqIndex& target_seqs_index)
{
  enum Column
  {
    QUERY_ID = 1,
    QUERY_LEN,
    QUERY_START,
    QUERY_END,
    STRAND,
    TARGET_ID,
    TARGET_LEN,
    TARGET_START,
    TARGET_END,
    RESIDUE_MATCHES,
    ALIGNMENT_LEN,
    MAPPING_QUAL
  };

  btllib::log_info(FN_NAME + ": Loading PAF mappings from " + filepath +
                   "... ");

  size_t n = 2048; // NOLINT
  char* line = new char[n];
  btllib::DataSource data_source(filepath);

  std::string token, mapped_seq_id, target_seq_id;
  while (getline(&line, &n, data_source) > 0) {
    if (line[0] == '@') {
      continue;
    }
    std::stringstream ss(line);
    Column column = QUERY_ID;
    while (bool(ss >> token)) {
      switch (column) {
        case QUERY_ID:
          mapped_seq_id = std::move(token);
          break;
        case TARGET_ID:
          target_seq_id = std::move(token);
          break;
        default: {
          break;
        }
      }
      column = Column(int(column) + 1);
    }
    load_mapping(mapped_seq_id, target_seq_id, target_seqs_index, 0);
  }
  btllib::log_info(FN_NAME + ": Done!");
}

unsigned
mapped_seqs_for_threshold(const std::vector<unsigned>& mx_in_common,
                          const unsigned mx_threshold)
{
  unsigned mapped_seq_num = 0;
  for (const auto mx : mx_in_common) {
    if (mx >= mx_threshold) {
      mapped_seq_num++;
    }
  }
  return mapped_seq_num;
}

void
AllMappings::filter(const double max_mapped_seqs_per_target_10kbp,
                    const unsigned mx_threshold_min,
                    const unsigned mx_threshold_max,
                    const SeqIndex& target_seqs_index)
{
  btllib::log_info(FN_NAME + ": Filtering contig mapped_seqs... ");

  btllib::check_error(max_mapped_seqs_per_target_10kbp <= 0,
                      FN_NAME +
                        ": max_mapped_seqs_per_target_10kbp is not positive.");
  btllib::check_error(
    mx_threshold_min >= mx_threshold_max,
    FN_NAME + ": mx_threshold_min is not smaller than mx_threshold_max.");

  for (auto& target_mappings : all_mappings) {
    const auto& target_seq_id = target_mappings.first;
    const auto& mappings = target_mappings.second;
    if (mappings.empty()) {
      continue;
    }
    const auto& mx_in_common = all_mx_in_common.at(target_seq_id);

    if (!target_seqs_index.seq_exists(target_seq_id)) {
      continue;
    }
    const auto target_seq_len = target_seqs_index.get_seq_len(target_seq_id);
    const int max_mapped_seqs = std::ceil(
      double(target_seq_len) * max_mapped_seqs_per_target_10kbp / 10'000.0);
    btllib::check_error(max_mapped_seqs <= 0,
                        FN_NAME + ": max_mapped_seqs <= 0.");

    int updated_mx_threshold_min = int(mx_threshold_min);
    int updated_mx_threshold_min_mapped_seqs = int(mappings.size());

    int updated_mx_threshold_max = int(mx_threshold_max);
    int updated_mx_threshold_max_mapped_seqs =
      int(mapped_seqs_for_threshold(mx_in_common, updated_mx_threshold_max));

    int mx_threshold = -1;
    if (updated_mx_threshold_min_mapped_seqs <= max_mapped_seqs) {
      mx_threshold = updated_mx_threshold_min;
    } else if (updated_mx_threshold_max_mapped_seqs > max_mapped_seqs) {
      mx_threshold = updated_mx_threshold_max;
    } else {
      while (updated_mx_threshold_max - updated_mx_threshold_min > 1) {
        const int mx_threshold_mid =
          (updated_mx_threshold_max + updated_mx_threshold_min) / 2;
        const int mx_threshold_mid_mapped_seqs =
          int(mapped_seqs_for_threshold(mx_in_common, mx_threshold_mid));
        if (mx_threshold_mid_mapped_seqs > max_mapped_seqs) {
          updated_mx_threshold_min = mx_threshold_mid;
          updated_mx_threshold_min_mapped_seqs = mx_threshold_mid_mapped_seqs;
        } else {
          updated_mx_threshold_max = mx_threshold_mid;
          updated_mx_threshold_max_mapped_seqs = mx_threshold_mid_mapped_seqs;
        }
      }
      mx_threshold = updated_mx_threshold_max;

      btllib::check_error(
        std::abs(updated_mx_threshold_min - updated_mx_threshold_max) > 1,
        FN_NAME +
          ": abs(updated_mx_threshold_min - updated_mx_threshold_max) > 1.");
      btllib::check_error(updated_mx_threshold_min_mapped_seqs <=
                            max_mapped_seqs,
                          FN_NAME + ": Min threshold wrongly calculated.");
      btllib::check_error(updated_mx_threshold_max_mapped_seqs >
                            max_mapped_seqs,
                          FN_NAME + ": Max threshold wrongly calculated.");
      btllib::check_error(
        updated_mx_threshold_min >= updated_mx_threshold_max,
        FN_NAME + ": updated_mx_threshold_min >= updated_mx_threshold_max.");
    }

    btllib::check_error(mx_threshold < 0, FN_NAME + ": mx_threshold < 0.");
    btllib::check_error(mx_threshold < int(mx_threshold_min),
                        FN_NAME + ": mx_threshold < mx_threshold_min.");
    btllib::check_error(mx_threshold > int(mx_threshold_max),
                        FN_NAME + ": mx_threshold > mx_threshold_max.");

    std::vector<SeqId> new_mappings;
    for (size_t i = 0; i < mappings.size(); i++) {
      if (int(mx_in_common[i]) >= mx_threshold) {
        new_mappings.push_back(mappings[i]);
      }
    }
    target_mappings.second = new_mappings;
  }
  btllib::log_info(FN_NAME + ": Done!");
}

const std::vector<SeqId>&
AllMappings::get_mappings(const std::string& id) const
{
  const auto it = all_mappings.find(id);
  if (it == all_mappings.end()) {
    return EMPTY_MAPPINGS;
  }
  return it->second;
}