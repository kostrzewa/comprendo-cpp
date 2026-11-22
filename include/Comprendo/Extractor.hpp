#pragma once

/// \file
/// \brief Interface of the structural extractors that turn a source tree into a chart.

#include <expected>
#include <string>
#include <string_view>

#include "Comprendo/Chart.hpp"
#include "Comprendo/ExtractConfig.hpp"

namespace Comprendo
{
  /// Builds the structural chart of a source tree.
  ///
  /// Implementations are deterministic: the same tree and configuration yield the
  /// same nodes and edges. Every node and edge they emit has provenance
  /// Provenance::Extractor.
  class Extractor
  {
  public:
    Extractor() = default;
    virtual ~Extractor() = default;
    Extractor(const Extractor &) = delete;
    Extractor &
    operator=(const Extractor &) = delete;
    Extractor(Extractor &&) = delete;
    Extractor &
    operator=(Extractor &&) = delete;

    /// Name of the backend, e.g. "ctags".
    ///
    /// \return Short backend name used in logs and the chart's generator field.
    [[nodiscard]]
    virtual std::string_view
    name() const = 0;

    /// Extract the chart of the tree at `config.root`.
    ///
    /// \param config Extraction settings.
    /// \return The chart in canonical order, or an error message.
    [[nodiscard]]
    virtual std::expected<Chart, std::string>
    extract(const ExtractConfig &config) = 0;
  };
} // namespace Comprendo
