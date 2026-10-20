#include "http/guarded.hpp"

#include "domain/errors.hpp"
#include "http/json_response.hpp"
#include "imaging/photo_pipeline.hpp"

namespace wcatalog {

void run_guarded(httplib::Response& res, const std::function<void()>& fn) {
    try {
        fn();
    } catch (const ValidationFailed& e) {
        send_validation_error(res, e.what(), e.fields);
    } catch (const PhotoRejected& e) {
        send_bad_request(res, e.what());
    } catch (const NotFoundError& e) {
        send_not_found(res, e.what());
    } catch (const ConflictError& e) {
        send_conflict(res, e.what());
    }
}

}  // namespace wcatalog
