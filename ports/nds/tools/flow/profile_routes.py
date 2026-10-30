"""Run an ordinary input route with complete N22 presentation/stage capture."""
import run_routes
from route_profile import install_route_profiling

install_route_profiling(run_routes.RouteObserver)
if __name__ == '__main__':
    raise SystemExit(run_routes.main())
