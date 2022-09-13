#ifndef CORECOMMON_SRC_BTREE_HPP_
#define CORECOMMON_SRC_BTREE_HPP_

#include <memory>
#include <optional>

#include "util.hpp"

template<class K, class V, class Compare=std::less<K>>
struct Node {
	struct Iterator {
		using iterator_category = std::input_iterator_tag;
		using difference_type = void;
		using value_type = Node;
		using pointer = Node*;

		Node* begin, *end;
		Node* current;

		void operator++() {
			if (current==end) {
				return;
			} else if (current->right) {
				current = current->right.get();
				while (current->left) current = current->left.get();
			} else {
				while (current->parent!=end && current->parent->right.get()==current) current = current->parent;
				current = current->parent;
			}
		}

		Iterator operator+(unsigned i) const {
			Iterator ret = *this;
			while ((i--)>0) ++ret;
			return ret;
		}

		std::unique_ptr<Node> consume() {
			std::unique_ptr<Node>& ptr = *current->parent_ptr();
			Node* ptr_p = current->parent;
			Root* root_p = current->root;

			this->operator++();
			if (ptr->left) ptr->left.reset();

			std::unique_ptr<Node> ret = std::unique_ptr<Node>(ptr.release());
			ptr.swap(ret->right);

			if (ptr) {
				ptr->parent = ptr_p;
				ptr->root = root_p;
			}

			ret->h_diff = 0;

			return ret;
		}

		void operator--() {
			if (current==end) {
				current = begin;
				while (current->right) current=current->right.get();
			} else if (current->left) {
				current = current->left.get();
				while (current->right) current = current->right.get();
			} else {
				while (current->parent!=end && current->parent->left.get()==current) current = current->parent;
				current = current->parent;
			}
		}

		Iterator operator-(unsigned i) const {
			Iterator ret = *this;
			while (i!=-1) --ret;
			return ret;
		}

		bool operator==(Iterator const& other) const {
			return other.current==current;
		}

		bool operator!=(Iterator const& other) const {
			return other.current!=current;
		}

		Node& operator*() {
			return *current;
		}

		Node* operator->() {
			return current;
		}
	};

	struct Root {
		std::unique_ptr<Node> ptr;

		Node* find(K const& fx) {
			return ptr ? ptr->find(fx) : nullptr;
		}

		void insert_node(std::unique_ptr<Node>&& node) {
			if (ptr) {
				node->root = nullptr;
				ptr->insert_node(std::move(node));
			} else {
				node->root = this;
				node->parent = nullptr;
				ptr.swap(node);
			}
		}

		Node* insert(K&& ix, V&& iv, Compare ins_cmp={}) {
			if (ptr) return ptr->insert(std::move(ix), std::move(iv), ins_cmp);
			else {
				ptr = std::make_unique<Node>(this, std::move(ix), std::move(iv), ins_cmp);
				return ptr.get();
			}
		}

		Iterator begin() {
			Iterator iter {.current=ptr.get(), .begin=ptr.get(), .end=nullptr};
			while (iter.current && iter.current->left) iter.current = iter.current->left.get();
			return iter;
		}

		Iterator end() {
			return {.current=nullptr, .begin=ptr.get(), .end=nullptr};
		}

		Iterator iter_ref(Node* ref) {
			return {.current=ref, .begin=ptr.get(), .end=nullptr};
		}

		void swap(Root& other) {
			ptr.swap(other.ptr);
			if (ptr) ptr->root=this;
			if (other.ptr) other.ptr->root=&other;
		}
	};

	Node* parent;
	Root* root;

	Compare cmp;

	char h_diff;

	K x;
	V v;

	std::unique_ptr<Node> left;
	std::unique_ptr<Node> right;

	Node(Root* parent, K&& k, V&& v, Compare cmp = {}): root(parent), parent(nullptr), h_diff(0), x(std::move(k)), v(std::move(v)), cmp(cmp) {}
	Node(Node* parent, K&& k, V&& v, Compare cmp = {}): root(nullptr), parent(parent), h_diff(0), x(std::move(k)), v(std::move(v)), cmp(cmp) {}
	Node(K&& k, V&& v, Compare cmp = {}): root(nullptr), parent(nullptr), h_diff(0), x(std::move(k)), v(std::move(v)), cmp(cmp) {}

	std::unique_ptr<Node>* parent_ptr() {
		if (parent) return parent->left.get()==this ? &parent->left : &parent->right;
		else if (root) return &root->ptr;
		else return nullptr;
	}

	Node* rot_right() {
		std::unique_ptr<Node>& ptr = *parent_ptr();
		ptr.release();
		ptr.swap(right);
		right.swap(ptr->left);
		ptr->left=std::unique_ptr<Node>(this);
		
		ptr->parent = parent;
		parent = ptr.get();

		std::swap(ptr->root, root);

		if (ptr->h_diff<=0) h_diff--;
		else h_diff -= ptr->h_diff+1;

		if (h_diff>=0) ptr->h_diff--;
		else ptr->h_diff += h_diff-1;

		return ptr.get();
	}

	Node* rot_left() {
		std::unique_ptr<Node>& ptr = *parent_ptr();

		ptr.release();
		ptr.swap(left);
		left.swap(ptr->right);
		ptr->right=std::unique_ptr<Node>(this);

		ptr->parent = parent;
		parent = ptr.get();

		std::swap(ptr->root, root);

		if (ptr->h_diff>=0) h_diff++;
		else h_diff -= ptr->h_diff-1;

		if (h_diff<=0) ptr->h_diff++;
		else ptr->h_diff += h_diff+1;

		return ptr.get();
	}

	Node* rebalance(bool r_inc) {
		Node* node = this;

		while (node) {
			bool is_right = node->parent && node->parent->right.get()==node;

			if (r_inc) {
				node->h_diff++;

				if (node->h_diff>=2) {
					if (node->right->h_diff==-1) node->right->rot_left();
					node=node->rot_right();

					if (node->h_diff==0) r_inc = is_right;
					else break;
				} else if (node->h_diff>0) {
					r_inc = is_right;
				}
			} else {
				node->h_diff--;

				if (node->h_diff<=-2) {
					if (node->left->h_diff==1) node->left->rot_right();
					node=node->rot_left();

					if (node->h_diff==0) r_inc = is_right;
					else break;
				} else if (node->h_diff<0) {
					r_inc = is_right;
				}
			}

			node=node->parent;
		}

		return node;
	}

	Node* find(K const& fx) {
		Node* node = this;

		while (true) {
			bool lt = cmp(fx,node->x);
			bool gt = cmp(node->x,fx);

			if (node->left && (lt || (!gt && !cmp(fx,node->left->x) && !cmp(node->left->x,fx)))) {
				node = node->left.get();
			} else if (gt && node->right) {
				node = node->right.get();
			} else {
				break;
			}
		}

		return node;
	}

	void insert_node(std::unique_ptr<Node>&& node) {
		Node* res = find(node->x);

		bool lt = cmp(node->x,res->x);
		if (lt) {
			res->left = std::move(node);
			res->left->parent = res;

			res->rebalance(false);
		} else if (cmp(res->x,node->x) || !res->right) {
			res->right = std::move(node);
			res->right->parent = res;

			res->rebalance(true);
		} else {
			res->right->insert_node(std::move(node));
		}
	}

	Node* insert(K&& ix, V&& iv, Compare ins_cmp={}) {
		std::unique_ptr<Node> node = std::make_unique<Node>(std::move(ix), std::move(iv), ins_cmp);
		Node* ret = node.get();
		insert_node(std::move(node));
		return ret;
	}

	std::unique_ptr<Node> remove() {
		std::unique_ptr<Node>& ptr = *parent_ptr();

		std::unique_ptr<Node> ret;
		if (left && !right) {
			ret.swap(left);
		} else if (right && !left) {
			ret.swap(right);
		}

		if (ret) {
			ret->parent = ptr->parent;
			ret->root = ptr->root;
		}

		if ((!right || !left) && parent) {
			parent->rebalance(parent->left.get()==this);
		}

		if (left && right) {
			std::unique_ptr<Node>* rl_ref = &right;
			while ((*rl_ref)->left) rl_ref = &(*rl_ref)->left;
			std::unique_ptr<Node>& rl = *rl_ref;

			bool left_child = rl!=right;

			//i dont know why this is so convoluted. who doesnt like swapping?!?/
			ret.swap(rl);

			Node* prev_parent = left_child ? ret->parent : ret.get();
			std::unique_ptr<Node> prev_r;

			if (ret->right) {
				rl.swap(ret->right);
				rl->parent = prev_parent;
			}

			if (right) {
				prev_r.swap(right);
				prev_r->parent = ret.get();
			}

			if (left) {
				ret->left.swap(left);
				ret->left->parent = ret.get();
			}

			ret->h_diff = h_diff;
			ret->right.swap(prev_r);

			ret->parent = ptr->parent;
			ret->root = ptr->root;
			ptr.swap(ret);

			prev_parent->rebalance(left_child);
		} else {
			ptr.swap(ret);
		}

		return ret;
	}

	Iterator begin() {
		Iterator iter {.current=this, .begin=this, .end=parent};
		while (iter.current->left) iter.current = iter.current->left.get();
		return iter;
	}

	Iterator end() {
		return {.current=parent, .begin=this, .end=parent};
	}

	Iterator iter_ref(Node* ref) {
		return {.current=ref, .begin=this, .end=parent};
	}

//	Node merge(Node* other) {
//		Node new_root;
//		for (Iterator iter = begin(); )
//	}
};

#endif