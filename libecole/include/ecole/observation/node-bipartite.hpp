#pragma once

#include <optional>

#include <xtensor/xtensor.hpp>

#include "ecole/export.hpp"
#include "ecole/observation/abstract.hpp"
#include "ecole/utility/sparse-matrix.hpp"

namespace ecole::observation {

struct ECOLE_EXPORT NodeBipartiteObs {
	using value_type = double;

	static inline std::size_t constexpr n_static_variable_features = 5;
	static inline std::size_t constexpr n_dynamic_variable_features = 15;
	static inline std::size_t constexpr n_variable_features = n_static_variable_features + n_dynamic_variable_features;
	enum struct ECOLE_EXPORT VariableFeatures : std::size_t {
		/** Static features */
		objective = 0,
		is_type_binary,            // One hot encoded
		is_type_integer,           // One hot encoded
		is_type_implicit_integer,  // One hot encoded
		is_type_continuous,        // One hot encoded

		/** Dynamic features */
		has_lower_bound,
		has_upper_bound,
		normed_reduced_cost,
		solution_value,
		solution_frac,
		is_solution_at_lower_bound,
		is_solution_at_upper_bound,
		scaled_age,
		incumbent_value,
		average_incumbent_value,
		is_basis_lower,  // One hot encoded
		is_basis_basic,  // One hot encoded
		is_basis_upper,  // One hot encoded
		is_basis_zero,   // One hot encoded
		index,
	};

	static inline std::size_t constexpr n_static_row_features = 2;
	static inline std::size_t constexpr n_dynamic_row_features = 3;
	static inline std::size_t constexpr n_row_features = n_static_row_features + n_dynamic_row_features;
	enum struct ECOLE_EXPORT RowFeatures : std::size_t {
		/** Static features */
		bias = 0,
		objective_cosine_similarity,

		/** Dynamic features */
		is_tight,
		dual_solution_value,
		scaled_age,
	};

	xt::xtensor<value_type, 2> variable_features;
	xt::xtensor<value_type, 2> row_features;
	utility::coo_matrix<value_type> edge_features;
};

class ECOLE_EXPORT NodeBipartite {
public:
	NodeBipartite(bool cache = false) : use_cache{cache} {}

	ECOLE_EXPORT auto before_reset(scip::Model& model) -> void;

	ECOLE_EXPORT auto extract(scip::Model& model, bool done) -> std::optional<NodeBipartiteObs>;

private:
	NodeBipartiteObs the_cache;
	bool use_cache = false;
	bool cache_computed = false;
};

}  // namespace ecole::observation
