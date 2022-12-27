#include "header.hpp"
#include "raster.hpp"

#include <CGAL/Polygon_mesh_processing/locate.h>
#include <Eigen/SparseQR>

namespace PMP = CGAL::Polygon_mesh_processing;

std::pair<K::FT, K::FT> road_width (std::pair<skeletonPoint,skeletonPoint> link);
std::pair<Surface_mesh::Face_index, Point_2> point_on_path_border(const Surface_mesh &mesh, Surface_mesh::Face_index face, std::list<K::Segment_2> segments);

std::set<std::pair<skeletonPoint,skeletonPoint>> link_paths(const Surface_mesh &mesh, const std::vector<std::list<Surface_mesh::Face_index>> &paths, const std::map<int, CGAL::Polygon_with_holes_2<Exact_predicates_kernel>> &path_polygon, const std::map<int, boost::shared_ptr<CGAL::Straight_skeleton_2<K>>> &medial_axes, const Raster &raster) {

	// Get label property
	Surface_mesh::Property_map<Surface_mesh::Face_index, unsigned char> label;
	bool has_label;
	boost::tie(label, has_label) = mesh.property_map<Surface_mesh::Face_index, unsigned char>("label");
	assert(has_label);

	std::set<std::pair<skeletonPoint,skeletonPoint>> result;

	for (int selected_label:  {3, 8, 9}) {
		// List path with selected label
		std::list<int> same_label_paths;
		for (int i = 0; i < paths.size(); i++) {
			if (label[paths[i].front()] == selected_label && medial_axes.count(i) == 1) {
				same_label_paths.push_back(i);
			}
		}

		for (int path1: same_label_paths) {
			for (int path2: same_label_paths) {
				if (path1 < path2) {

					std::map<std::pair<CGAL::Straight_skeleton_2<K>::Vertex_handle, CGAL::Straight_skeleton_2<K>::Vertex_handle>, K::FT> distance_v1v2;
					std::map<std::pair<CGAL::Straight_skeleton_2<K>::Vertex_handle, CGAL::Straight_skeleton_2<K>::Halfedge_handle>, std::pair<K::FT, K::Point_2>> distance_v1h2;
					std::map<std::pair<CGAL::Straight_skeleton_2<K>::Vertex_handle, CGAL::Straight_skeleton_2<K>::Halfedge_handle>, std::pair<K::FT, K::Point_2>> distance_v2h1;

					// For vertices pairs
					for (auto v1: medial_axes.at(path1)->vertex_handles()) {
						if (v1->is_skeleton()) {
							for (auto v2: medial_axes.at(path2)->vertex_handles()) {
								if (v2->is_skeleton()) {
									distance_v1v2[std::make_pair(v1, v2)] = CGAL::squared_distance(v1->point(), v2->point());
								}
							}
						}
					}

					// For vertex on path1 and edge on path2
					for (auto v1: medial_axes.at(path1)->vertex_handles()) {
						if (v1->is_skeleton()) {
							for (auto edge2: medial_axes.at(path2)->halfedge_handles()) {
								if (edge2->vertex()->id() < edge2->opposite()->vertex()->id() && edge2->is_inner_bisector() && edge2->opposite()->is_inner_bisector()) {
									auto segment = K::Segment_2(edge2->opposite()->vertex()->point(), edge2->vertex()->point());
									auto proj = segment.supporting_line().projection(v1->point());
									if (segment.collinear_has_on(proj)) {
										distance_v1h2[std::make_pair(v1, edge2)] = std::make_pair(CGAL::squared_distance(v1->point(), proj), proj);
									}
								}
							}
						}
					}

					// For vertex on path2 and edge on path1
					for (auto v2: medial_axes.at(path2)->vertex_handles()) {
						if (v2->is_skeleton()) {
							for (auto edge1: medial_axes.at(path1)->halfedge_handles()) {
								if (edge1->vertex()->id() < edge1->opposite()->vertex()->id() && edge1->is_inner_bisector() && edge1->opposite()->is_inner_bisector()) {
									auto segment = K::Segment_2(edge1->opposite()->vertex()->point(), edge1->vertex()->point());
									auto proj = segment.supporting_line().projection(v2->point());
									if (segment.collinear_has_on(proj)) {
										distance_v2h1[std::make_pair(v2, edge1)] = std::make_pair(CGAL::squared_distance(v2->point(), proj), proj);
									}
								}
							}
						}
					}

					// For vertices pairs
					for (auto it = distance_v1v2.begin(); it != distance_v1v2.end(); ++it) {
						auto v1 = it->first.first;
						auto v2 = it->first.second;
						auto d = it->second;

						auto he = v1->halfedge_around_vertex_begin();
						do {
							auto v = (*he)->opposite()->vertex();
							if (v->is_skeleton()) {
								if (distance_v1v2[std::make_pair(v, v2)] < d) {
									goto exit1;
								}
								if ((*he)->vertex()->id() < (*he)->opposite()->vertex()->id()) {
									if (distance_v2h1.count(std::make_pair(v2, *he)) > 0 && distance_v2h1[std::make_pair(v2, *he)].first < d) {
										goto exit1;
									}
								} else {
									if (distance_v2h1.count(std::make_pair(v2, (*he)->opposite())) > 0 && distance_v2h1[std::make_pair(v2, (*he)->opposite())].first < d) {
										goto exit1;
									}
								}
							}
						} while (++he != v1->halfedge_around_vertex_begin());

						he = v2->halfedge_around_vertex_begin();
						do {
							auto v = (*he)->opposite()->vertex();
							if (v->is_skeleton()) {
								if (distance_v1v2[std::make_pair(v1, v)] < d) {
									goto exit1;
								}
								if ((*he)->vertex()->id() < (*he)->opposite()->vertex()->id()) {
									if (distance_v1h2.count(std::make_pair(v1, *he)) > 0 && distance_v1h2[std::make_pair(v1, *he)].first < d) {
										goto exit1;
									}
								} else {
									if (distance_v1h2.count(std::make_pair(v1, (*he)->opposite())) > 0 && distance_v1h2[std::make_pair(v1, (*he)->opposite())].first < d) {
										goto exit1;
									}
								}
							}
						} while (++he != v2->halfedge_around_vertex_begin());

						result.insert(std::make_pair(skeletonPoint(path1, v1), skeletonPoint(path2, v2)));
						exit1:
							continue;
					}

					// For vertex on path1 and edge on path2
					for (auto it = distance_v1h2.begin(); it != distance_v1h2.end(); ++it) {
						auto v1 = it->first.first;
						auto e2 = it->first.second;
						auto d = it->second.first;
						auto p2 = it->second.second;
									
						auto he = v1->halfedge_around_vertex_begin();
						do {
							auto v = (*he)->opposite()->vertex();
							if (v->is_skeleton()) {
								if (CGAL::squared_distance(v->point(), p2) < d) {
									goto exit2;
								}
								auto segment = K::Segment_2((*he)->opposite()->vertex()->point(), (*he)->vertex()->point());
								auto proj = segment.supporting_line().projection(p2);
								if (segment.collinear_has_on(proj)) {
									goto exit2;
								}
							}
						} while (++he != v1->halfedge_around_vertex_begin());

						result.insert(std::make_pair(skeletonPoint(path1, v1), skeletonPoint(path2, e2, p2)));
						exit2:
							continue;
					}

					// For vertex on path2 and edge on path1
					for (auto it = distance_v2h1.begin(); it != distance_v2h1.end(); ++it) {
						auto v2 = it->first.first;
						auto e1 = it->first.second;
						auto d = it->second.first;
						auto p1 = it->second.second;
									
						auto he = v2->halfedge_around_vertex_begin();
						do {
							auto v = (*he)->opposite()->vertex();
							if (v->is_skeleton()) {
								if (CGAL::squared_distance(v->point(), p1) < d) {
									goto exit3;
								}
								auto segment = K::Segment_2((*he)->opposite()->vertex()->point(), (*he)->vertex()->point());
								auto proj = segment.supporting_line().projection(p1);
								if (segment.collinear_has_on(proj)) {
									goto exit3;
								}
							}	
						} while (++he != v2->halfedge_around_vertex_begin());

						result.insert(std::make_pair(skeletonPoint(path2, v2), skeletonPoint(path1, e1, p1)));
						exit3:
							continue;
					}

				} else if (path1 == path2) {

					std::map<std::pair<CGAL::Straight_skeleton_2<K>::Vertex_handle, CGAL::Straight_skeleton_2<K>::Vertex_handle>, K::FT> distance_vv;
					std::map<std::pair<CGAL::Straight_skeleton_2<K>::Vertex_handle, CGAL::Straight_skeleton_2<K>::Halfedge_handle>, std::pair<K::FT, K::Point_2>> distance_vh;

					for (auto v1: medial_axes.at(path1)->vertex_handles()) {
						if (v1->is_skeleton()) {
							// For vertices pairs
							for (auto v2: medial_axes.at(path1)->vertex_handles()) {
								if (v2->is_skeleton()) {
									distance_vv[std::make_pair(v1, v2)] = CGAL::squared_distance(v1->point(), v2->point());
								}
							}
							// For vertex and edge
							for (auto edge2: medial_axes.at(path1)->halfedge_handles()) {
								if (edge2->vertex()->id() < edge2->opposite()->vertex()->id() && edge2->is_inner_bisector() && edge2->opposite()->is_inner_bisector()) {
									auto segment = K::Segment_2(edge2->opposite()->vertex()->point(), edge2->vertex()->point());
									auto proj = segment.supporting_line().projection(v1->point());
									if (segment.collinear_has_on(proj)) {
										distance_vh[std::make_pair(v1, edge2)] = std::make_pair(CGAL::squared_distance(v1->point(), proj), proj);
									}
								}
							}
						}
					}

					// For vertices pairs
					for (auto it = distance_vv.begin(); it != distance_vv.end(); ++it) {
						auto v1 = it->first.first;
						auto v2 = it->first.second;
						auto d = it->second;

						if (v1->id() == v2->id()) continue;

						// Exit link
						Exact_predicates_kernel::Segment_2 segment(Exact_predicates_kernel::Point_2(v1->point().x(), v1->point().y()), Exact_predicates_kernel::Point_2(v2->point().x(), v2->point().y()));
						bool intersect = false;
						for (auto edge = path_polygon.at(path1).outer_boundary().edges_begin(); !intersect && edge != path_polygon.at(path1).outer_boundary().edges_end(); edge++) {
							if (CGAL::do_intersect(*edge, segment)) {
								intersect = true;
							}
						}
						for (auto hole: path_polygon.at(path1).holes()) {
							for (auto edge = hole.edges_begin(); !intersect && edge != hole.edges_end(); edge++) {
								if (CGAL::do_intersect(*edge, segment)) {
									intersect = true;
								}
							}
						}
						if (!intersect) continue;

						auto he = v1->halfedge_around_vertex_begin();
						do {
							auto v = (*he)->opposite()->vertex();
							if (v->is_skeleton()) {
								if (distance_vv[std::make_pair(v, v2)] < d) {
									goto exit4;
								}
								if ((*he)->vertex()->id() < (*he)->opposite()->vertex()->id()) {
									if (distance_vh.count(std::make_pair(v2, *he)) > 0 && distance_vh[std::make_pair(v2, *he)].first < d) {
										goto exit4;
									}
								} else {
									if (distance_vh.count(std::make_pair(v2, (*he)->opposite())) > 0 && distance_vh[std::make_pair(v2, (*he)->opposite())].first < d) {
										goto exit4;
									}
								}
							}
						} while (++he != v1->halfedge_around_vertex_begin());

						he = v2->halfedge_around_vertex_begin();
						do {
							auto v = (*he)->opposite()->vertex();
							if (v->is_skeleton()) {
								if (distance_vv[std::make_pair(v1, v)] < d) {
									goto exit4;
								}
								if ((*he)->vertex()->id() < (*he)->opposite()->vertex()->id()) {
									if (distance_vh.count(std::make_pair(v1, *he)) > 0 && distance_vh[std::make_pair(v1, *he)].first < d) {
										goto exit4;
									}
								} else {
									if (distance_vh.count(std::make_pair(v1, (*he)->opposite())) > 0 && distance_vh[std::make_pair(v1, (*he)->opposite())].first < d) {
										goto exit4;
									}
								}
							}
						} while (++he != v2->halfedge_around_vertex_begin());

						result.insert(std::make_pair(skeletonPoint(path1, v1), skeletonPoint(path1, v2)));
						exit4:
							continue;
					}

					// For vertex on path1 and edge on path2
					for (auto it = distance_vh.begin(); it != distance_vh.end(); ++it) {
						auto v1 = it->first.first;
						auto e2 = it->first.second;
						auto d = it->second.first;
						auto p2 = it->second.second;

						if (v1->id() == e2->vertex()->id() || v1->id() == e2->opposite()->vertex()->id()) continue;

						// Exit link
						Exact_predicates_kernel::Segment_2 segment(Exact_predicates_kernel::Point_2(v1->point().x(), v1->point().y()), Exact_predicates_kernel::Point_2(p2.x(), p2.y()));
						bool intersect = false;
						for (auto edge = path_polygon.at(path1).outer_boundary().edges_begin(); !intersect && edge != path_polygon.at(path1).outer_boundary().edges_end(); edge++) {
							if (CGAL::do_intersect(*edge, segment)) {
								intersect = true;
							}
						}
						for (auto hole: path_polygon.at(path1).holes()) {
							for (auto edge = hole.edges_begin(); !intersect && edge != hole.edges_end(); edge++) {
								if (CGAL::do_intersect(*edge, segment)) {
									intersect = true;
								}
							}
						}
						if (!intersect) continue;
									
						auto he = v1->halfedge_around_vertex_begin();
						do {
							auto v = (*he)->opposite()->vertex();
							if (v->is_skeleton()) {
								if (CGAL::squared_distance(v->point(), p2) < d) {
									goto exit5;
								}
								auto segment = K::Segment_2((*he)->opposite()->vertex()->point(), (*he)->vertex()->point());
								auto proj = segment.supporting_line().projection(p2);
								if (segment.collinear_has_on(proj)) {
									goto exit5;
								}
							}
						} while (++he != v1->halfedge_around_vertex_begin());

						result.insert(std::make_pair(skeletonPoint(path1, v1), skeletonPoint(path1, e2, p2)));
						exit5:
							continue;
					}
				}
			}
		}
	}

	Surface_mesh links;

	for(auto link: result) {
		auto z1 = raster.dsm[int(link.first.point.y())][int(link.first.point.x())];
		auto z2 = raster.dsm[int(link.second.point.y())][int(link.second.point.x())];
		auto v1 = links.add_vertex(Point_3((float) link.first.point.x(), (float) link.first.point.y(), z1));
		auto v2 = links.add_vertex(Point_3((float) link.second.point.x(), (float) link.second.point.y(), z2));
		links.add_edge(v1,v2);
	}

	bool created;
	Surface_mesh::Property_map<Surface_mesh::Edge_index, int> edge_prop;
	boost::tie(edge_prop, created) = links.add_property_map<Surface_mesh::Edge_index, int>("prop",0);
	assert(created);

	double min_x, min_y;
	raster.grid_to_coord(0, 0, min_x, min_y);

	for(auto vertex : links.vertices()) {
		auto point = links.point(vertex);
		double x, y;
		raster.grid_to_coord((float) point.x(), (float) point.y(), x, y);
		links.point(vertex) = Point_3(x-min_x, y-min_y, point.z());
	}

	std::ofstream mesh_ofile ("links.ply");
	CGAL::IO::write_PLY (mesh_ofile, links);
	mesh_ofile.close();

	return result;

}

void bridge (std::pair<skeletonPoint,skeletonPoint> link, const Surface_mesh &mesh, const Raster &raster) {
	std::cout << "Bridge " << link.first.path << " (" << link.first.point << ") -> " << link.second.path << " (" << link.second.point << ")\n";


	Surface_mesh::Property_map<Surface_mesh::Face_index, int> path;
	bool has_path;
	boost::tie(path, has_path) = mesh.property_map<Surface_mesh::Face_index, int>("path");
	assert(has_path);

	Surface_mesh::Property_map<Surface_mesh::Face_index, unsigned char> label;
	bool has_label;
	boost::tie(label, has_label) = mesh.property_map<Surface_mesh::Face_index, unsigned char>("label");
	assert(has_label);

	typedef CGAL::Face_filtered_graph<Surface_mesh> Filtered_graph;
	Filtered_graph filtered_sm1(mesh, link.first.path, path);
	Filtered_graph filtered_sm2(mesh, link.second.path, path);

	typedef CGAL::dynamic_vertex_property_t<Point_2>               Point_2_property;
	auto projection_pmap = CGAL::get(Point_2_property(), mesh);
	
	for(auto v : CGAL::vertices(mesh)) {
		const Point_3& p = mesh.point(v);
		put(projection_pmap, v, Point_2(p.x(), p.y()));
	}

	auto location1 = PMP::locate(link.first.point, filtered_sm1, CGAL::parameters::vertex_point_map(projection_pmap));
	auto location2 = PMP::locate(link.second.point, filtered_sm2, CGAL::parameters::vertex_point_map(projection_pmap));
	auto point1 = PMP::construct_point(location1, mesh);
	auto point2 = PMP::construct_point(location2, mesh);

	unsigned char bridge_label = label[location1.first];

	K::Vector_2 link_vector(link.first.point, link.second.point);
	float length = sqrt(link_vector.squared_length());
	auto l = link_vector / length;
	auto n = l.perpendicular(CGAL::COUNTERCLOCKWISE);

	auto width = road_width(link);

	Point_2 left1 = link.first.point - n*width.first/2;
	Point_2 right1 = link.first.point + n*width.first/2;
	Point_2 left2 = link.second.point - n*width.second/2;
	Point_2 right2 = link.second.point + n*width.second/2;

	auto left1_border = point_on_path_border(mesh, location1.first, {K::Segment_2(link.first.point, left1)});
	auto right1_border = point_on_path_border(mesh, location1.first, {K::Segment_2(link.first.point, right1)});
	auto left2_border = point_on_path_border(mesh, location2.first, {K::Segment_2(link.second.point, left2)});
	auto right2_border = point_on_path_border(mesh, location2.first, {K::Segment_2(link.second.point, right2)});

	float dl0 = sqrt(CGAL::squared_distance(link.first.point, left1_border.second));
	float dr0 = sqrt(CGAL::squared_distance(link.first.point, right1_border.second));
	float dlN = sqrt(CGAL::squared_distance(link.second.point, left2_border.second));
	float drN = sqrt(CGAL::squared_distance(link.second.point, right2_border.second));

	int N = int(sqrt(link_vector.squared_length ()));
	float xl[N+1];
	float xr[N+1];
	float z_segment[N+1];
	for (int i = 0; i <= N; i++) {
		xl[i] = dl0 + ((float) i)/N*(dlN-dl0);
		xr[i] = dr0 + ((float) i)/N*(drN-dr0);
		z_segment[i] = point1.z() + (point2.z() - point1.z())*((float) i)/N;
	}

	float tunnel_height = 3; // in meter

	for(int loop = 0; loop < 10; loop++) {

		std::cout << "Solve surface\n";

		// Surface solver
		{
			std::vector<Eigen::Triplet<float>> tripletList;
			tripletList.reserve(10*N+2);
			std::vector<float> temp_b;
			temp_b.reserve(9*N+2);

			// regularity of the surface
			float alpha = 10;
			for (int i = 0; i < N; i++) {
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), i, alpha));
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), i+1, -alpha));
				temp_b.push_back(0);
			}

			// attachment to DSM data
			float beta = 1;
			for (int i = 0; i <= N; i++) {
				int j = std::max({0, ((int) (- xl[i]))});
				while(j <= xr[i]) {
					auto p = link.first.point + ((float) i)/N*link_vector + j * n;
					if (p.y() >= 0 && p.y() < raster.ySize && p.x() >= 0 && p.y() < raster.xSize) {
						if (z_segment[i] > raster.dsm[p.y()][p.x()] - tunnel_height / 2) {
							tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), i, beta));
							temp_b.push_back(raster.dsm[p.y()][p.x()]);
						} else if (z_segment[i] > raster.dsm[p.y()][p.x()] - tunnel_height) {
							tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), i, beta));
							temp_b.push_back(raster.dsm[p.y()][p.x()] - tunnel_height);
						}
					}
					j++;
				}
				j = std::max({1, ((int) (- xr[i]))});
				while(j <= xl[i]) {
					auto p = link.first.point + ((float) i)/N*link_vector - j * n;
					if (p.y() >= 0 && p.y() < raster.ySize && p.x() >= 0 && p.y() < raster.xSize) {
						if (z_segment[i] > raster.dsm[p.y()][p.x()] - tunnel_height / 2) {
							tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), i, beta));
							temp_b.push_back(raster.dsm[p.y()][p.x()]);
						} else if (z_segment[i] > raster.dsm[p.y()][p.x()] - tunnel_height) {
							tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), i, beta));
							temp_b.push_back(raster.dsm[p.y()][p.x()] - tunnel_height);
						}
					}
					j++;
				}
			}

			// border
			float zeta = 10;
			tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), 0, zeta));
			temp_b.push_back(zeta * point1.z());
			tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), N, zeta));
			temp_b.push_back(zeta * point2.z());

			// solving
			Eigen::SparseMatrix<float> A(temp_b.size(), N+1);
			A.setFromTriplets(tripletList.begin(), tripletList.end());
			Eigen::VectorXf b = Eigen::Map<Eigen::VectorXf, Eigen::Unaligned>(temp_b.data(), temp_b.size());

			Eigen::SparseQR<Eigen::SparseMatrix<float>, Eigen::COLAMDOrdering<int>> solver;
			solver.compute(A);
			if(solver.info()!=Eigen::Success) {
				std::cout << "decomposition failed\n";
				return;
			}
			Eigen::VectorXf x = solver.solve(b);
			if(solver.info()!=Eigen::Success) {
				std::cout << "solving failed\n";
				return;
			}
			for (int i = 0; i <= N; i++) {
				z_segment[i] = x[i];
			}
		}

		std::cout << "Solve contour\n";

		// Contour solver
		{
			std::vector<Eigen::Triplet<float>> tripletList;
			tripletList.reserve(2*N + N+1 + 4 + 4);
			std::vector<float> temp_b;
			temp_b.reserve(2*N + N+1 + 4 + 4);

			//regularity of the contour
			float gamma = 5;
			for (int j = 0; j < N; j++) {
				// x^l_j − x^l_{j+1}
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), j,gamma));
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), j+1,-gamma));
				temp_b.push_back(0);
			}
			for (int j = N+1; j < 2*N+1; j++) {
				// x^r_{j-N-1} − x^r_{j+1-N-1}
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), j,gamma));
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), j+1,-gamma));
				temp_b.push_back(0);
			}

			//width of the reconstructed surface
			float delta = 10;
			for (int j = 0; j <= N; j++) {
				// x^l_j − x^l_{j+1}
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), j,delta)); //x^l_j
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), j+N+1,delta)); //x^r_j
				temp_b.push_back(delta * (width.first + j*(width.second - width.first)/N));
			}

			//centering of the surface on the link vertices
			float epsilon = 1;
			tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), 0,epsilon)); //x^l_0
			tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), N+1,-epsilon)); //x^r_0
			temp_b.push_back(0);
			tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), N,epsilon)); //x^l_N
			tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), 2*N+1,-epsilon)); //x^r_N
			temp_b.push_back(0);

			//keeping surface where cost is low
			float theta = 1;
			float iota  = 10;
			// left contour
			for (int i = 0; i <= N; i++) {
				float j = xl[i];
				float j_min = j;
				float v_min;
				auto p = link.first.point + ((float) i)/N*link_vector + j * n;
				if (p.y() >= 0 && p.y() < raster.ySize && p.x() >= 0 && p.y() < raster.xSize) {
					if (z_segment[i] <= raster.dsm[p.y()][p.x()] - tunnel_height) {
						continue;
					} else if (z_segment[i] <= raster.dsm[p.y()][p.x()] - tunnel_height/2) {
						v_min = pow(z_segment[i] - raster.dsm[p.y()][p.x()] + tunnel_height, 2);
					} else {
						v_min = pow(z_segment[i] - raster.dsm[p.y()][p.x()], 2);
						if (raster.land_cover[p.y()][p.x()] != bridge_label) {
							if ((raster.land_cover[p.y()][p.x()] != 8 && raster.land_cover[p.y()][p.x()] != 9) || (bridge_label != 8 && bridge_label != 9)) {
								v_min += theta;
							}
						}
					}
				} else {
					continue;
				}

				j--;
				while(j > -xr[i]) {
					auto p = link.first.point + ((float) i)/N*link_vector + j * n;
					if (p.y() >= 0 && p.y() < raster.ySize && p.x() >= 0 && p.y() < raster.xSize) {
						if (z_segment[i] <= raster.dsm[p.y()][p.x()] - tunnel_height) {
							j_min = j;
							break;
						} else if (z_segment[i] <= raster.dsm[p.y()][p.x()] - tunnel_height/2) {
							float v = pow(z_segment[i] - raster.dsm[p.y()][p.x()] + tunnel_height, 2);
							if (v < v_min) {
								j_min = j;
								v_min = v;
							}
						} else {
							float v = pow(z_segment[i] - raster.dsm[p.y()][p.x()], 2);
							if (raster.land_cover[p.y()][p.x()] != bridge_label) {
								if ((raster.land_cover[p.y()][p.x()] != 8 && raster.land_cover[p.y()][p.x()] != 9) || (bridge_label != 8 && bridge_label != 9)) {
									v += theta;
								}
							}
							if (v < v_min) {
								j_min = j;
								v_min = v;
							}
						}
					} else {
						break;
					}
					j--;
				}
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), i, iota));
				temp_b.push_back(iota * j_min);
			}
			// right contour
			for (int i = 0; i <= N; i++) {
				float j = xr[i];
				float j_min = j;
				float v_min;
				auto p = link.first.point + ((float) i)/N*link_vector + j * n;
				if (p.y() >= 0 && p.y() < raster.ySize && p.x() >= 0 && p.y() < raster.xSize) {
					if (z_segment[i] <= raster.dsm[p.y()][p.x()] - tunnel_height) {
						continue;
					} else if (z_segment[i] <= raster.dsm[p.y()][p.x()] - tunnel_height/2) {
						v_min = pow(z_segment[i] - raster.dsm[p.y()][p.x()] + tunnel_height, 2);
					} else {
						v_min = pow(z_segment[i] - raster.dsm[p.y()][p.x()], 2);
						if (raster.land_cover[p.y()][p.x()] != bridge_label) {
							if ((raster.land_cover[p.y()][p.x()] != 8 && raster.land_cover[p.y()][p.x()] != 9) || (bridge_label != 8 && bridge_label != 9)) {
								v_min += theta;
							}
						}
					}
				} else {
					continue;
				}

				j--;
				while(j > -xl[i]) {
					auto p = link.first.point + ((float) i)/N*link_vector + j * n;
					if (p.y() >= 0 && p.y() < raster.ySize && p.x() >= 0 && p.y() < raster.xSize) {
						if (z_segment[i] <= raster.dsm[p.y()][p.x()] - tunnel_height) {
							j_min = j;
							break;
						} else if (z_segment[i] <= raster.dsm[p.y()][p.x()] - tunnel_height/2) {
							float v = pow(z_segment[i] - raster.dsm[p.y()][p.x()] + tunnel_height, 2);
							if (v < v_min) {
								j_min = j;
								v_min = v;
							}
						} else {
							float v = pow(z_segment[i] - raster.dsm[p.y()][p.x()], 2);
							if (raster.land_cover[p.y()][p.x()] != bridge_label) {
								if ((raster.land_cover[p.y()][p.x()] != 8 && raster.land_cover[p.y()][p.x()] != 9) || (bridge_label != 8 && bridge_label != 9)) {
									v += theta;
								}
							}
							if (v < v_min) {
								j_min = j;
								v_min = v;
							}
						}
					} else {
						break;
					}
					j--;
				}
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), i + N+1, iota));
				temp_b.push_back(iota * j_min);
			}

			// fix border
			float eta = 100;
			if (xl[0] > dl0) {
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), 0, eta)); //x^l_0
				temp_b.push_back(eta*dl0);
			}
			if (xl[N] > dlN) {
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), N, eta)); //x^l_N
				temp_b.push_back(eta*dlN);
			}
			if (xr[0] > dr0) {
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), N+1, eta)); //x^r_0
				temp_b.push_back(eta*dr0);
			}
			if (xr[N] > drN) {
				tripletList.push_back(Eigen::Triplet<float>(temp_b.size(), 2*N+1, eta)); //x^r_N
				temp_b.push_back(eta*drN);
			}

			// solving
			Eigen::SparseMatrix<float> A(temp_b.size(),2*N+2);
			A.setFromTriplets(tripletList.begin(), tripletList.end());
			Eigen::VectorXf b = Eigen::Map<Eigen::VectorXf, Eigen::Unaligned>(temp_b.data(), temp_b.size());
			
			Eigen::SparseQR<Eigen::SparseMatrix<float>, Eigen::COLAMDOrdering<int>> solver;
			solver.compute(A);
			if(solver.info()!=Eigen::Success) {
				std::cout << "decomposition failed\n";
				return;
			}
			Eigen::VectorXf x = solver.solve(b);
			if(solver.info()!=Eigen::Success) {
				std::cout << "solving failed\n";
				return;
			}
			for (int j = 0; j < N; j++) {
				// x^l_j
				xl[j] = x[j];
				// x^r_j
				xr[j] = x[j+N+1];
			}

		}
	}
	
	if (xl[0] > dl0) xl[0] = dl0;
	if (xl[N] > dlN) xl[N] = dlN;
	if (xr[0] > dr0) xr[0] = dr0;
	if (xr[N] > drN) xr[N] = drN;

	std::cout << "Save\n";

	{ // Surface

		Surface_mesh bridge_mesh;
		Surface_mesh::Vertex_index Xl[N+1];
		Surface_mesh::Vertex_index Xr[N+1];

		// Add points
		for (int i = 0; i <= N; i++) {
			auto p1 = link.first.point + ((float) i)/N*link_vector - xl[i] * n;
			auto p2 = link.first.point + ((float) i)/N*link_vector + xr[i] * n;
			Xl[i] = bridge_mesh.add_vertex(Point_3(p1.x(), p1.y(), z_segment[i]));
			Xr[i] = bridge_mesh.add_vertex(Point_3(p2.x(), p2.y(), z_segment[i]));
		}

		// Add faces
		for (int i = 0; i < N; i++) {
			bridge_mesh.add_face(Xl[i], Xr[i], Xr[i+1]);
			bridge_mesh.add_face(Xl[i], Xr[i+1], Xl[i+1]);
		}

		double min_x, min_y;
		raster.grid_to_coord(0, 0, min_x, min_y);

		for(auto vertex : bridge_mesh.vertices()) {
			auto point = bridge_mesh.point(vertex);
			double x, y;
			raster.grid_to_coord((float) point.x(), (float) point.y(), x, y);
			bridge_mesh.point(vertex) = Point_3(x-min_x, y-min_y, point.z());
		}

		std::stringstream bridge_mesh_name;
		bridge_mesh_name << "bridge_mesh_" << ((int) bridge_label) << "_" << link.first.path << "_" << link.second.path << "_" << link.first.point << "_" << link.second.point << ".ply";
		std::ofstream mesh_ofile (bridge_mesh_name.str().c_str());
		CGAL::IO::write_PLY (mesh_ofile, bridge_mesh);
		mesh_ofile.close();

		std::cout << bridge_mesh_name.str() << " saved\n";

	}


	{ // Skeleton
		Surface_mesh skeleton;

		auto v1 = skeleton.add_vertex(point1);
		auto v2 = skeleton.add_vertex(point2);
		skeleton.add_edge(v1, v2);

		v1 = skeleton.add_vertex(Point_3(left1_border.second.x(), left1_border.second.y(), point1.z()));
		v2 = skeleton.add_vertex(Point_3(left2_border.second.x(), left2_border.second.y(), point2.z()));
		skeleton.add_edge(v1, v2);

		v1 = skeleton.add_vertex(Point_3(right1_border.second.x(), right1_border.second.y(), point1.z()));
		v2 = skeleton.add_vertex(Point_3(right2_border.second.x(), right2_border.second.y(), point2.z()));
		skeleton.add_edge(v1, v2);

		v1 = skeleton.add_vertex(Point_3(left1.x(), left1.y(), point1.z()));
		v2 = skeleton.add_vertex(Point_3(right1.x(), right1.y(), point1.z()));
		skeleton.add_edge(v1, v2);

		v1 = skeleton.add_vertex(Point_3(left2.x(), left2.y(), point2.z()));
		v2 = skeleton.add_vertex(Point_3(right2.x(), right2.y(), point2.z()));
		skeleton.add_edge(v1, v2);

		/*auto init = skeleton.add_vertex(Point_3(Xr[0].x(), Xr[0].y(), point1.z()));
		auto temp1 = CGAL::SM_Vertex_index(init);
		for (int i = 1; i <= N; i++) {
			auto temp2 = skeleton.add_vertex(Point_3(Xr[i].x(), Xr[i].y(), point1.z()));
			skeleton.add_edge(temp1, temp2);
			temp1 = temp2;
		}
		for (int i = N; i >= 0; i--) {
			auto temp2 = skeleton.add_vertex(Point_3(Xl[i].x(), Xl[i].y(), point1.z()));
			skeleton.add_edge(temp1, temp2);
			temp1 = temp2;
		}
		skeleton.add_edge(temp1, init);*/

		Surface_mesh::Property_map<Surface_mesh::Edge_index, int> edge_prop;
		bool created;
		boost::tie(edge_prop, created) = skeleton.add_property_map<Surface_mesh::Edge_index, int>("prop",0);
		assert(created);

		double min_x, min_y;
		raster.grid_to_coord(0, 0, min_x, min_y);

		for(auto vertex : skeleton.vertices()) {
			auto point = skeleton.point(vertex);
			double x, y;
			raster.grid_to_coord((float) point.x(), (float) point.y(), x, y);
			skeleton.point(vertex) = Point_3(x-min_x, y-min_y, point.z());
		}

		std::stringstream skeleton_name;
		skeleton_name << "bridge_" << ((int) bridge_label) << "_" << link.first.path << "_" << link.second.path << "_" << link.first.point << "_" << link.second.point << ".ply";
		std::ofstream mesh_ofile (skeleton_name.str().c_str());
		CGAL::IO::write_PLY (mesh_ofile, skeleton);
		mesh_ofile.close();
	}

}