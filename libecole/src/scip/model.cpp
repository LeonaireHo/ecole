#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <string>

#include <scip/scip.h>
#include <scip/scipdefplugins.h>

#include "ecole/exception.hpp"
#include "ecole/scip/model.hpp"

#include "scip/utils.hpp"

namespace ecole {
namespace scip {

template <> void Deleter<SCIP>::operator()(SCIP* scip) {
	scip::call(SCIPfree, &scip);
}

unique_ptr<SCIP> create() {
	SCIP* scip_raw;
	scip::call(SCIPcreate, &scip_raw);
	SCIPmessagehdlrSetQuiet(SCIPgetMessagehdlr(scip_raw), true);
	auto scip_ptr = unique_ptr<SCIP>{};
	scip_ptr.reset(scip_raw);
	return scip_ptr;
}

unique_ptr<SCIP> copy(SCIP const* source) {
	if (!source) return nullptr;
	if (SCIPgetStage(const_cast<SCIP*>(source)) == SCIP_STAGE_INIT) return create();
	auto dest = create();
	// Copy operation is not thread safe
	static std::mutex m{};
	std::lock_guard<std::mutex> g{m};
	scip::call(
		SCIPcopy,
		const_cast<SCIP*>(source),
		dest.get(),
		nullptr,
		nullptr,
		"",
		true,
		false,
		false,
		nullptr);
	return dest;
}

SCIP* Model::get_scip_ptr() const noexcept {
	return scip.get();
}

Model::Model() : scip(create()) {
	scip::call(SCIPincludeDefaultPlugins, get_scip_ptr());
}

Model::Model(unique_ptr<SCIP>&& scip) {
	if (scip)
		this->scip = std::move(scip);
	else
		throw Exception("Cannot create empty model");
}

Model::Model(Model const& other) : scip(copy(other.get_scip_ptr())) {}

Model& Model::operator=(Model const& other) {
	if (&other != this) scip = copy(other.get_scip_ptr());
	return *this;
}

bool Model::operator==(Model const& other) const noexcept {
	return scip == other.scip;
}

bool Model::operator!=(Model const& other) const noexcept {
	return !(*this == other);
}

Model Model::from_file(const std::string& filename) {
	auto model = Model{};
	model.read_prob(filename);
	return model;
}

void Model::read_prob(std::string const& filename) {
	scip::call(SCIPreadProb, get_scip_ptr(), filename.c_str(), nullptr);
}

// Assumptions made while defining ParamType
static_assert(
	std::is_same<SCIP_Bool, param_t<ParamType::Bool>>::value,
	"SCIP bool type is not the same as the one redefined by Ecole");
static_assert(
	std::is_same<SCIP_Longint, param_t<ParamType::LongInt>>::value,
	"SCIP long int type is not the same as the one redefined by Ecole");
static_assert(
	std::is_same<SCIP_Real, param_t<ParamType::Real>>::value,
	"SCIP real type is not the same as the one redefined by Ecole");

ParamType Model::get_param_type(const char* name) const {
	auto* scip_param = SCIPgetParam(get_scip_ptr(), name);
	if (!scip_param)
		throw make_exception(SCIP_PARAMETERUNKNOWN);
	else
		switch (SCIPparamGetType(scip_param)) {
		case SCIP_PARAMTYPE_BOOL:
			return ParamType::Bool;
		case SCIP_PARAMTYPE_INT:
			return ParamType::Int;
		case SCIP_PARAMTYPE_LONGINT:
			return ParamType::LongInt;
		case SCIP_PARAMTYPE_REAL:
			return ParamType::Real;
		case SCIP_PARAMTYPE_CHAR:
			return ParamType::Char;
		case SCIP_PARAMTYPE_STRING:
			return ParamType::String;
		default:
			assert(false);  // All enum value should be handled
			// Non void return for optimized build
			throw Exception("Could not find type for given parameter");
		}
}

ParamType Model::get_param_type(std::string const& name) const {
	return get_param_type(name.c_str());
}

param_t<ParamType::Int> Model::seed() const {
	return get_param_explicit<param_t<ParamType::Int>>("randomization/randomseedshift");
}

template <typename T> static auto mod(T num, T div) noexcept {
	return (num % div + div) % div;
}

void Model::seed(param_t<ParamType::Int> seed_v) {
	using seed_t = param_t<ParamType::Int>;
	set_param_explicit<seed_t>("randomization/randomseedshift", std::abs(seed_v));
}

void Model::solve() {
	scip::call(SCIPsolve, get_scip_ptr());
}

void Model::interrupt_solve() {
	scip::call(SCIPinterruptSolve, get_scip_ptr());
}

void Model::disable_presolve() {
	scip::call(SCIPsetPresolving, get_scip_ptr(), SCIP_PARAMSETTING_OFF, true);
}
void Model::disable_cuts() {
	scip::call(SCIPsetSeparating, get_scip_ptr(), SCIP_PARAMSETTING_OFF, true);
}

bool Model::is_solved() const noexcept {
	return SCIPgetStage(get_scip_ptr()) == SCIP_STAGE_SOLVED;
}

VarView Model::variables() const noexcept {
	auto const scip_ptr = get_scip_ptr();
	auto const n_vars = static_cast<std::size_t>(SCIPgetNVars(scip_ptr));
	return VarView(scip_ptr, SCIPgetVars(scip_ptr), n_vars);
}

VarView Model::lp_branch_cands() const noexcept {
	int n_vars{};
	SCIP_VAR** vars{};
	scip::call(
		SCIPgetLPBranchCands,
		get_scip_ptr(),
		&vars,
		nullptr,
		nullptr,
		&n_vars,
		nullptr,
		nullptr);
	return VarView(get_scip_ptr(), vars, static_cast<std::size_t>(n_vars));
}

ColView Model::lp_columns() const {
	auto const scip_ptr = get_scip_ptr();
	if (SCIPgetStage(scip_ptr) != SCIP_STAGE_SOLVING)
		throw Exception("LP columns are only available during solving");
	auto const n_cols = static_cast<std::size_t>(SCIPgetNLPCols(scip_ptr));
	return ColView(scip_ptr, SCIPgetLPCols(scip_ptr), n_cols);
}

RowView Model::lp_rows() const {
	auto const scip_ptr = get_scip_ptr();
	if (SCIPgetStage(scip_ptr) != SCIP_STAGE_SOLVING)
		throw Exception("LP rows are only available during solving");
	auto const n_rows = static_cast<std::size_t>(SCIPgetNLPRows(scip_ptr));
	return RowView(scip_ptr, SCIPgetLPRows(scip_ptr), n_rows);
}

void Model::include_branchrule(std::unique_ptr<::scip::ObjBranchrule>&& branchrule) {
	scip::call(SCIPincludeObjBranchrule, get_scip_ptr(), branchrule.release(), true);
}

namespace internal {

template <> void set_scip_param(SCIP* scip, const char* name, SCIP_Bool value) {
	scip::call(SCIPsetBoolParam, scip, name, value);
}
template <> void set_scip_param(SCIP* scip, const char* name, char value) {
	scip::call(SCIPsetCharParam, scip, name, value);
}
template <> void set_scip_param(SCIP* scip, const char* name, int value) {
	scip::call(SCIPsetIntParam, scip, name, value);
}
template <> void set_scip_param(SCIP* scip, const char* name, SCIP_Longint value) {
	scip::call(SCIPsetLongintParam, scip, name, value);
}
template <> void set_scip_param(SCIP* scip, const char* name, SCIP_Real value) {
	scip::call(SCIPsetRealParam, scip, name, value);
}
template <> void set_scip_param(SCIP* scip, const char* name, const char* value) {
	scip::call(SCIPsetStringParam, scip, name, value);
}
template <> void set_scip_param(SCIP* scip, const char* name, std::string const& value) {
	return set_scip_param(scip, name, value.c_str());
}

template <> SCIP_Bool get_scip_param(SCIP* scip, const char* name) {
	SCIP_Bool value{};
	scip::call(SCIPgetBoolParam, scip, name, &value);
	return value;
}
template <> char get_scip_param(SCIP* scip, const char* name) {
	char value{};
	scip::call(SCIPgetCharParam, scip, name, &value);
	return value;
}
template <> int get_scip_param(SCIP* scip, const char* name) {
	int value{};
	scip::call(SCIPgetIntParam, scip, name, &value);
	return value;
}
template <> SCIP_Longint get_scip_param(SCIP* scip, const char* name) {
	SCIP_Longint value{};
	scip::call(SCIPgetLongintParam, scip, name, &value);
	return value;
}
template <> SCIP_Real get_scip_param(SCIP* scip, const char* name) {
	SCIP_Real value{};
	scip::call(SCIPgetRealParam, scip, name, &value);
	return value;
}
template <> const char* get_scip_param(SCIP* scip, const char* name) {
	char* ptr = nullptr;
	scip::call(SCIPgetStringParam, scip, name, &ptr);
	return ptr;
}

template <> Cast_SFINAE<char, const char*>::operator char() const {
	if (std::strlen(val) == 1)
		return *val;
	else
		throw scip::Exception("Can only convert a string with a single character to a char");
}

}  // namespace internal

}  // namespace scip
}  // namespace ecole
