/*
 * Copyright (c) 2009, Jianing Yang<jianingy.yang@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The names of its contributors may not be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY detrox@gmail.com ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL detrox@gmail.com BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <iostream>
#include <cstdio>

#include "trie_impl.h"

#define sanity_delete(X)  do { \
    if (X) { \
        delete (X); \
        (X) = NULL; \
    }} while (0);

BEGIN_TRIE_NAMESPACE

const char double_trie::magic_[16] = "TWO_TRIE";
const char single_trie::magic_[16] = "TAIL_TRIE";

// ************************************************************************
// * Implementation of helper functions                                   *
// ************************************************************************

static const char* pretty_size(size_t size, char *buf, size_t buflen)
{
    assert(buf);
    if (size > 1024 * 1024 * 1024) {
        snprintf(buf, buflen, "%4.2fG",
                 static_cast<double>(size) / (1024 * 1024 * 1024));
    } else if (size > 1024 * 1024) {
        snprintf(buf, buflen, "%4.2fM",
                 static_cast<double>(size) / (1024 * 1024));
    } else if (size > 1024) {
        snprintf(buf, buflen, "%4.2fK", static_cast<double>(size) / 1024);
    } else {
        snprintf(buf, buflen, "%4.2f", static_cast<double>(size));
    }
    return buf;
}

trie::~trie()
{
}

// ************************************************************************
// * Implementation of basic_trie                                         *
// ************************************************************************

basic_trie::basic_trie(size_type size,
                       trie_relocator_interface<size_type> *relocator)
    :header_(NULL), states_(NULL), last_base_(0), max_state_(0), owner_(true),
     relocator_(relocator)
{
    if (size < key_type::kCharsetSize)
        size = kDefaultStateSize;
    header_ = new header_type();
    memset(header_, 0, sizeof(header_type));
    resize_state(size);
}

basic_trie::basic_trie(void *header, void *states)
    :header_(NULL), states_(NULL), last_base_(0), max_state_(0), owner_(false),
     relocator_(NULL)
{
    header_ = static_cast<header_type *>(header);
    states_ = static_cast<state_type *>(states);
}

basic_trie::basic_trie(const basic_trie &trie)
    :header_(NULL), states_(NULL), last_base_(0), max_state_(0), owner_(false),
     relocator_(NULL)
{
    clone(trie);
}

basic_trie &basic_trie::operator=(const basic_trie &trie)
{
    clone(trie);
    return *this;
}

void basic_trie::clone(const basic_trie &trie)
{
    if (owner_) {
        if (header_) {
            sanity_delete(header_);
        }
        if (states_) {
            resize(states_, 0, 0);
            states_ = NULL;  // set to NULL for next resize
        }
    }
    owner_ = true;
    max_state_ = trie.max_state();
    header_ = new header_type();
    states_ = resize(states_, 0, trie.header()->size);
    memcpy(header_, trie.header(), sizeof(header_type));
    memcpy(states_, trie.states(), trie.header()->size * sizeof(state_type));
}

basic_trie::~basic_trie()
{
    if (owner_) {
        sanity_delete(header_);
        resize(states_, 0, 0);  // free states_
    }
}

//返回可用的偏移基址，但并没有对base,check写入
trie::size_type
basic_trie::find_base(const char_type *inputs, const extremum_type &extremum)
{
    bool found;
    size_type i;
    const char_type *p;

    for (i = last_base_, found = false; !found; /* empty */) {
        i++;
        if (i + extremum.max >= header_->size)
            resize_state(extremum.max);
        if (check(i + extremum.min) <= 0 && check(i + extremum.max) <= 0) {
            for (p = inputs, found = true; *p; p++) {
                if (check(i + *p) > 0) {
                    found = false;
                    break;
                }
            }
        } else {
            //  i += extremum.min;
        }
    }

    last_base_ = (i > 256)?i - 255:i;

    return i;
}

trie::size_type
basic_trie::relocate(size_type stand,
                     size_type s,
                     const char_type *inputs,
                     const extremum_type &extremum)
{
    size_type obase, nbase, i;
    char_type targets[key_type::kCharsetSize + 1];

    obase = base(s);  // save old base value
    nbase = find_base(inputs, extremum);  // find a new base

    for (i = 0; inputs[i]; i++) {
        if (check(obase + inputs[i]) != s)  // find old links
            continue;
        set_base(nbase + inputs[i], base(obase + inputs[i]));//保证子状态偏移基址不变，不影响后--后面状态的状态编号,即存储位置不变，不用更新
        set_check(nbase + inputs[i], check(obase + inputs[i]));//父状态号s没有改变，改变的是父状态的偏移基址
        find_exist_target(obase + inputs[i], targets, NULL);//找到原来的孙子状态编号(因为子状态偏移基址不变，所以孙子状态编号也不变)
        //因为子状态编号变了，所以孙子状态的父状态编号要更新
        for (char_type *p = targets; *p; p++) {
            set_check(base(obase + inputs[i]) + *p, nbase + inputs[i]);
        }
        // if where we are standing is moving, we move with it
        if (stand == obase + inputs[i])
            stand = nbase + inputs[i];
        if (relocator_)
            relocator_->relocate(obase + inputs[i], nbase + inputs[i]);
        // free old places
        set_base(obase + inputs[i], 0);
        set_check(obase + inputs[i], 0);
        // create new links according old ones
    }
    // finally, set new base
    set_base(s, nbase);

    return stand;
}

//包含了解决冲突的情况
trie::size_type
basic_trie::create_transition(size_type s, char_type ch)
{
    char_type targets[key_type::kCharsetSize + 1];
    char_type parent_targets[key_type::kCharsetSize + 1];
    extremum_type extremum = {0, 0}, parent_extremum = {0, 0};

    size_type t = next(s, ch);
    if (t >= header_->size)
        resize_state(t - header_->size + 1);

    if (base(s) > 0 && check(t) <= 0) {//base[s]>0,意味着s还不是终点（构成了一个词）
        // Do Nothing !!
    } else {
        //解决冲突，改变base[s],还是改变base[check[t]],取决于谁的下一个状态数少，就改变谁的base[]，这样操作更少
        size_type num_targets =
            find_exist_target(s, targets, &extremum);
        size_type num_parent_targets = check(t)?
            find_exist_target(check(t), parent_targets, &parent_extremum):0;
        if (num_parent_targets > 0 && num_targets + 1 > num_parent_targets) {
            //s比check[t]的目标状态多，改变base[check[t]]
            s = relocate(s, check(t), parent_targets, parent_extremum);
        } else {
            targets[num_targets++] = ch;
            targets[num_targets] = 0;
            if (ch > extremum.max || !extremum.max)
                extremum.max = ch;
            if (ch < extremum.min || !extremum.min)
                extremum.min = ch;
            s = relocate(s, s, targets, extremum);
        }
        t = next(s, ch);
        if (t >= header_->size)
            resize_state(t - header_->size + 1);
    }
    set_check(t, s);

    return t;
}
//value不是偏移基址?
//关键词key(比如"hello")所设置的value就是base[hello]里的值，所以value代表key所对应的偏移基址。
void basic_trie::insert(const key_type &key, const value_type &value)
{
    //value为偏移基址
    if (value < 1)
        throw std::runtime_error("basic_trie::insert: value must > 0");

    const char_type *p = NULL;
    size_type s = go_forward(1, key.data(), &p);
    do {
        s = create_transition(s, *p);
    } while (*p++ != key_type::kTerminator);
    //s对应abcdef(Terminator)，value设置为负值，代表单词结束标记？不可为负，必须大于0
    set_base(s, value);
}

//value对应key状态的偏移基址,并不是？并不是，value是与key对应的值，比如员工编号，解释含义的下标
bool basic_trie::search(const key_type &key, value_type *value) const
{
    const char_type *p = NULL;
    size_type s = go_forward(1, key.data(), &p);
    if (p)
        return false;
    if (value)
        *value = base(s);
    return true;
}

size_t
basic_trie::prefix_search(const key_type &prefix, result_type *result) const
{
    const char_type *p;
    size_type s = go_forward(1, prefix.data(), &p);
    key_type store(prefix);
    prefix_search_aux(s, p, &store, result);
    return result->size();
}
//保存前缀(对应状态s)后面的所有到终点的分支，和对应的base值,即查找所有前缀是s的key及base值
size_t basic_trie::prefix_search_aux(size_type s,
                                     const char_type *miss,
                                     key_type *store,
                                     result_type *result) const
{
    char_type targets[key_type::kCharsetSize + 1];

    if (find_exist_target(s, targets, NULL)) {
        for (char_type *p = targets; *p; p++) {
            if (miss && *miss != key_type::kTerminator && *miss != *p)
                continue;
            size_type t = next(s, *p);
            store->push(*p);
            if (!miss || *miss == key_type::kTerminator)
                prefix_search_aux(t, miss, store, result);
            else
                prefix_search_aux(t, miss + 1, store, result);
            store->pop();
        }
    } else {
        result->push_back(std::pair<key_type, value_type>(*store, base(s)));
    }
    return 0;
}

//打印从s开始所有字符串
void basic_trie::trace(size_type s) const
{
    size_type num_target;
    char_type targets[key_type::kCharsetSize + 1];
    static std::vector<size_type> trace_stack;

    trace_stack.push_back(s);
    if ((num_target = find_exist_target(s, targets, NULL))) {
        for (char_type *p = targets; *p; p++) {
            size_type t = next(s, *p);
            if (t < header_->size)
                trace(next(s, *p));
        }
    } else {
        size_type cbase = 0, obase = 0;
        std::cerr << "transition => ";
        std::vector<size_type>::const_iterator it;
        for (it = trace_stack.begin();it != trace_stack.end(); it++) {
            cbase = base(*it);
            if (obase) {
                if (*it - obase == key_type::kTerminator) {
                    std::cerr << "-#->";
                } else {
                    char ch = key_type::char_out(*it - obase);
                    if (isgraph(ch))
                        std::cerr << "-'" << ch << "'->";
                    else
                        std::cerr << "-<" << std::hex
                                  << static_cast<uint8_t>(ch) << ">->";
                }
            }
            std::clog << *it << "[" << cbase << "]";
            obase = cbase;
        }
        std::cerr << "->{" << std::dec << (cbase) << "}" << std::endl;
    }
    trace_stack.pop_back();
}

// ************************************************************************
// * Implementation of two trie                                           *
// ************************************************************************

double_trie::double_trie(size_t size)
    :header_(NULL), lhs_(NULL), rhs_(NULL), index_(NULL), accept_(NULL),
     next_accept_(1), next_index_(1), front_relocator_(NULL),
     rear_relocator_(NULL), mmap_(NULL), mmap_size_(0)
{
    header_ = new header_type();
    memset(header_, 0, sizeof(header_type));
    snprintf(header_->magic, sizeof(header_->magic), "%s", magic_);
    front_relocator_ = new trie_relocator<double_trie>
                           (this, &double_trie::relocate_front);
    rear_relocator_ = new trie_relocator<double_trie>
                          (this, &double_trie::relocate_rear);
    lhs_ = new basic_trie(size);
    rhs_ = new basic_trie(size);
    lhs_->set_relocator(front_relocator_);
    rhs_->set_relocator(rear_relocator_);
    header_->index_size = size?size:basic_trie::kDefaultStateSize;
    index_ = resize(index_, 0, header_->index_size);
    header_->accept_size = size?size:basic_trie::kDefaultStateSize;
    accept_ = resize(accept_, 0, header_->accept_size);
    watcher_[0] = 0;
    watcher_[1] = 0;
}

double_trie::double_trie(const char *filename)
    :header_(NULL), lhs_(NULL), rhs_(NULL), index_(NULL), accept_(NULL),
     next_accept_(1), next_index_(1), front_relocator_(NULL),
     rear_relocator_(NULL), mmap_(NULL), mmap_size_(0)
{
    struct stat sb;
    int fd, retval;

    if (!filename)
        throw std::runtime_error(std::string("can not load from file ")
                                 + filename);

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        throw std::runtime_error(strerror(errno));
    if (fstat(fd, &sb) < 0)
        throw std::runtime_error(strerror(errno));

    mmap_ = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmap_ == MAP_FAILED)
        throw std::runtime_error(strerror(errno));
    while (retval = close(fd), retval == -1 && errno == EINTR) {
        // exmpty
    }
    mmap_size_ = sb.st_size;

    void *start;
    start = header_ = reinterpret_cast<header_type *>(mmap_);
    if (strcmp(header_->magic, magic_))
        throw std::runtime_error("file corrupted");
    // load index
    start = index_ = reinterpret_cast<index_type *>(
                     reinterpret_cast<header_type *>(start) + 1);
    // load accept
    start = accept_ = reinterpret_cast<accept_type *>(
                      reinterpret_cast<index_type *>(start)
                      + header_->index_size);
    // load front trie
    start = reinterpret_cast<accept_type *>(start) + header_->accept_size;
    lhs_ = new basic_trie(start,
                          reinterpret_cast<basic_trie::header_type *>(start)
                          + 1);
    // load rear trie
    start = reinterpret_cast<basic_trie::state_type *>
            ((basic_trie::header_type *)start + 1)
            + lhs_->header()->size;
    rhs_ = new basic_trie(start,
                          reinterpret_cast<basic_trie::header_type *>(start)
                          + 1);
}


double_trie::~double_trie()
{
    if (mmap_) {
        if (munmap(mmap_, mmap_size_) < 0)
            throw std::runtime_error(strerror(errno));
    } else {
        sanity_delete(header_);
        resize(index_, 0, 0);  // free index_
        resize(accept_, 0, 0);  // free accept_
        sanity_delete(front_relocator_);
        sanity_delete(rear_relocator_);
    }
    sanity_delete(lhs_);
    sanity_delete(rhs_);
}

trie::size_type
double_trie::rhs_append(const char_type *inputs)
{
    const char_type *p;
    size_type s = 1, t;

    s = rhs_->go_forward_reverse(s, inputs, &p);
    if (!p) {  // all characters match
        if (outdegree(s) == 0) {
            return s;
        } else {
            t = rhs_->next(s, key_type::kTerminator);
            if (!rhs_->check_transition(s, t))
                return rhs_->create_transition(s, key_type::kTerminator);
            return t;
        }
    }
    if (outdegree(s) == 0) {
        t = rhs_->create_transition(s, key_type::kTerminator);
        std::set<size_type>::const_iterator it;
        if (refer_.find(s) != refer_.end()) {
            for (it = refer_[s].referer.begin();
                    it != refer_[s].referer.end();
                    it++) {
                set_link(*it, t);
            }
            free_accept_entry(s);
        }
    }
    do {
        s = rhs_->create_transition(s, *p);
    } while (p-- > inputs);
    return s;
}

void
double_trie::lhs_insert(size_type s, const char_type *inputs, value_type value)
{
    size_t i;
    s = lhs_->create_transition(s, inputs[0]);
    if (*inputs == key_type::kTerminator) {
        i = find_index_entry(s);
        index_[i].index = 0;
    } else {
        i = set_link(s, rhs_append(inputs + 1));
    }
    index_[i].data = value;
}

void double_trie::rhs_clean_more(size_type t)
{
    if (outdegree(t) == 0 && count_referer(t) == 0) {
        assert(rhs_->check(t) > 0);
        size_type s = rhs_->prev(t);
        remove_accept_state(t);
        assert(s > 0);
        rhs_clean_more(s);
    } else if (outdegree(t) == 1) {
        size_type r = rhs_->next(t, key_type::kTerminator);
        if (rhs_->check_transition(t, r)) {
            // delete transition 't -#-> r'
            if (refer_.find(r) != refer_.end()) {
                std::set<size_type>::const_iterator it;
                for (it = refer_[r].referer.begin();
                        it != refer_[r].referer.end();
                        it++)
                    set_link(*it, t);
                assert (refer_.find(t) != refer_.end());
                accept_[refer_[t].accept_index].accept = t;
            }
            if (rhs_->base(r) > 1)
                rhs_->set_last_base(rhs_->base(r));
            remove_accept_state(r);
        }
    }
}

void double_trie::rhs_insert(size_type s, size_type r,
                             const std::vector<char_type> &match,
                             const char_type *remain,
                             char_type ch, size_type value)
{
    // R-1
    size_type u = link_state(s);
    assert(u > 0);
    assert(rhs_->check(u) > 0);
    value_type oval = index_[-lhs_->base(s)].data;
    index_[-lhs_->base(s)].index = 0;
    index_[-lhs_->base(s)].data = 0;
    free_index_.push_back(-lhs_->base(s));
    // s is separator which implies base(s) < 0, so we need to set base(s) = 0
    lhs_->set_base(s, 0);
    watcher_[0] = u; // u & r may be changed during rhs_->create_transition
    watcher_[1] = r; // we use watcher_ to monitor there changing.
    if (refer_.find(u) != refer_.end()) {
        refer_[u].referer.erase(s);

        if (refer_[u].referer.size() == 0)
            free_accept_entry(u);
    }

    // R-2
    std::vector<char_type>::const_iterator it;
    for (it = match.begin(); it != match.end(); it++) {
        s = lhs_->create_transition(s, *it);
    }

    size_type t = lhs_->create_transition(s, *remain);
    size_type i;
    if (*remain == key_type::kTerminator) {
        i = find_index_entry(t);
        index_[-lhs_->base(t)].data = value;
        index_[-lhs_->base(t)].index = 0;
    } else {
        size_type a = rhs_append(remain + 1);
        assert(rhs_->check(watcher_[0]) > 0);
        i = set_link(t, a);
        index_[i].data = value;
    }

    // R-3
    t = lhs_->create_transition(s, ch);
    size_type v = rhs_->prev(watcher_[1]);  // v -ch-> r
    if (!rhs_->check_transition(v, rhs_->next(v, key_type::kTerminator)))
        r = rhs_->create_transition(v, key_type::kTerminator);
    else
        r = rhs_->next(v, key_type::kTerminator);
    i = set_link(t, r);
    index_[i].data = oval;

    // R-4
    u = watcher_[0];
    if (!rhs_clean_one(u))
        rhs_clean_more(u);
}

void double_trie::insert(const key_type &key, const value_type &value)
{
    const char_type *p;
    size_type s = lhs_->go_forward(1, key.data(), &p);

    if (!p) {
        // duplicated key found
        index_[-lhs_->base(s)].data = value;
        return;
    }

    if (!check_separator(s)) {
        lhs_insert(s, p, value);
        return;
    }
    assert(index_[-lhs_->base(s)].index > 0);
    size_type r = link_state(s);
    // skip dummy terminator
    if (rhs_->check_reverse_transition(r, key_type::kTerminator)
        && rhs_->prev(r) > 1)
        r = rhs_->prev(r);

    // travel reversely
    exists_.clear();
    do {
        if (rhs_->check_reverse_transition(r, *p)) {
            r = rhs_->prev(r);
            exists_.push_back(*p);
        } else {
            break;
        }
        if (r == 1) {  // duplicated key
            index_[-lhs_->base(s)].data = value;
            return;
        }
    } while (*p++ != key_type::kTerminator);
    char_type mismatch = r - rhs_->base(rhs_->prev(r));
    rhs_insert(s, r, exists_, p, mismatch, value);
    return;
}

bool double_trie::search(const key_type &key, value_type *value) const
{
    const char_type *p, *mismatch;
    size_type s = lhs_->go_forward(1, key.data(), &p);
    if (!p) {
        if (value)
            *value = index_[-lhs_->base(s)].data;
        return true;
    }
    if (!check_separator(s))
        return false;
    assert(index_[-lhs_->base(s)].index > 0);
    size_type r = link_state(s);
    // skip a terminator
    if (rhs_->check_reverse_transition(r, key_type::kTerminator))
        r = rhs_->prev(r);
    r = rhs_->go_backward(r, p, &mismatch);
    if (r == 1) {
        if (value)
            *value = index_[-lhs_->base(s)].data;
        return true;
    }
    return false;
}

size_t
double_trie::prefix_search(const key_type &key, result_type *result) const
{
    const char_type *p;
    size_type s = lhs_->go_forward(1, key.data(), &p);
    key_type store;
    if (lhs_->check_reverse_transition(s, key_type::kTerminator))
        s = lhs_->prev(s);
    if (p)
        store.assign(key.data(), p - key.data());
    else
        store.assign(key.data(), key.length());
    lhs_->prefix_search_aux(s, p, &store, result);
    result_type::iterator it;
    for (it = result->begin(); it != result->end(); it++) {
        size_t i = -it->second;
        if (index_[i].index == 0) {
            it->second = index_[i].data;
            continue;
        }
        const char_type *miss = p;
        bool fail = false;
        size_type r = accept_[index_[i].index].accept;
        // skip a terminator
        if (rhs_->check_reverse_transition(r, key_type::kTerminator))
            r = rhs_->prev(r);
        do {
            char_type ch = r - rhs_->base(rhs_->prev(r));
            r = rhs_->prev(r);
            if (miss && *miss != key_type::kTerminator) {
                if (!rhs_->check_transition(r, rhs_->next(r, *miss))) {
                    fail = true;
                    break;
                }
                miss++;
            }
            it->first.push(ch);
        } while (r > 1);
        if (fail || (miss && *miss != key_type::kTerminator)) {
            --it;
            result->erase(it + 1);
            continue;
        }
        it->second = index_[i].data;
    }

    return result->size();
}

void double_trie::build(const char *filename, bool verbose)
{
    FILE *out;

    if (!filename)
        throw std::runtime_error(std::string("can not save to file ")
                                 + filename);

    if ((out = fopen(filename, "w+"))) {
        header_->index_size = next_index_;
        header_->accept_size = next_accept_;
        fwrite(header_, sizeof(header_type), 1, out);
        fwrite(index_, sizeof(index_type) * header_->index_size, 1, out);
        fwrite(accept_, sizeof(accept_type) * header_->accept_size, 1, out);
        fwrite(lhs_->compact_header(),
               sizeof(basic_trie::header_type), 1, out);
        fwrite(lhs_->states(), sizeof(basic_trie::state_type)
                               * lhs_->compact_header()->size, 1, out);
        fwrite(rhs_->compact_header(),
               sizeof(basic_trie::header_type), 1, out);
        fwrite(rhs_->states(), sizeof(basic_trie::state_type)
                               * rhs_->compact_header()->size, 1, out);
        fclose(out);
        if (verbose) {
            char buf[256];
            size_t size[4];
            size[0] = sizeof(index_type) * header_->index_size;
            size[1] = sizeof(accept_type) * header_->accept_size;
            size[2] = sizeof(basic_trie::state_type)
                      * lhs_->compact_header()->size;
            size[3] = sizeof(basic_trie::state_type)
                      * rhs_->compact_header()->size;

            std::cerr << "index = "
                      << pretty_size(size[0], buf, sizeof(buf));
            std::cerr << ", accept = "
                      << pretty_size(size[1], buf, sizeof(buf));
            std::cerr << ", front = "
                      << pretty_size(size[2], buf, sizeof(buf));
            std::cerr << ", rear = "
                      << pretty_size(size[3], buf, sizeof(buf));
            std::cerr << ", total = "
                      << pretty_size(size[0] + size[1] + size[2] + size[3],
                                     buf, sizeof(buf))
                      << std::endl;
        }
    }
}

// ************************************************************************
// * Implementation of suffix trie                                        *
// ************************************************************************

single_trie::single_trie(size_t size)
    :trie_(NULL), suffix_(NULL), header_(NULL), next_suffix_(1),
     mmap_(NULL), mmap_size_(0)
{
    trie_ = new basic_trie(size);
    header_ = new header_type();
    memset(&common_, 0, sizeof(common_));
    resize_suffix(size?size:basic_trie::kDefaultStateSize);
    resize_common(kDefaultCommonSize);
}

single_trie::single_trie(const char *filename)
    :trie_(NULL), suffix_(NULL), header_(NULL), next_suffix_(1),
     mmap_(NULL), mmap_size_(0)
{
    struct stat sb;
    int fd, retval;

    if (!filename)
        throw std::runtime_error(std::string("can not load from file ")
                                 + filename);

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        throw std::runtime_error(strerror(errno));
    if (fstat(fd, &sb) < 0)
        throw std::runtime_error(strerror(errno));

    mmap_ = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mmap_ == MAP_FAILED)
        throw std::runtime_error(strerror(errno));
    while (retval = close(fd), retval == -1 && errno == EINTR) {
        // exmpty
    }
    mmap_size_ = sb.st_size;

    void *start;
    start = header_ = reinterpret_cast<header_type *>(mmap_);
    if (strcmp(header_->magic, magic_))
        throw std::runtime_error("file corrupted");
    // load suffix
    suffix_ = reinterpret_cast<suffix_type *>(
              reinterpret_cast<header_type *>(start) + 1);
    // load trie
    start = suffix_ + header_->suffix_size;
    trie_ = new basic_trie(start,
                          reinterpret_cast<basic_trie::header_type *>(start)
                          + 1);
}


single_trie::~single_trie()
{
    if (mmap_) {
        if (munmap(mmap_, mmap_size_) < 0)
            throw std::runtime_error(strerror(errno));
    } else {
        sanity_delete(header_);
        resize(suffix_, 0, 0);   // free suffix_
        resize(common_.data, 0, 0);  // free common_.data
    }
    sanity_delete(trie_);
}

//value可以作为每个单词的编号?
void single_trie::insert_suffix(size_type s,
                                const char_type *inputs,
                                value_type value)
{
    trie_->set_base(s, -next_suffix_);
    const char_type *p = inputs;
    do {
        // +1 for value
        if (next_suffix_ + 1 >= header_->suffix_size)
            resize_suffix(next_suffix_ + 1);
        suffix_[next_suffix_++] = *p;
    } while (*p++ != key_type::kTerminator);
    suffix_[next_suffix_++] = value;
}

void single_trie::create_branch(size_type s,
                                const char_type *inputs,
                                value_type value)
{
    basic_trie::extremum_type extremum = {0, 0};
    size_type start = -trie_->base(s);

    // find common string
    const char_type *p = inputs;
    size_t i = 0;
    do {
        if (suffix_[start] != *p)
            break;
        if (i + 1 >= common_.size)
            resize_common(i + 1);
        common_.data[i++] = *p;
        if (*p > extremum.max || !extremum.max)
            extremum.max = *p;
        if (*p < extremum.min || !extremum.min)
            extremum.min = *p;
        ++start;
    } while (*p++ != key_type::kTerminator);
    common_.data[i] = 0;  // end common string

    // check if already exists by checking if the last common char is
    // terminator
    if (i > 0 && common_.data[i - 1] == key_type::kTerminator) {
        // duplicated key
        suffix_[start] = value;
        return;
    }

    // if there is a common part, insert common string into trie
    if (common_.data[0]) {
        trie_->set_base(s, trie_->find_base(common_.data, extremum));
        for (i = 0; common_.data[i]; i++)
            s = trie_->create_transition(s, common_.data[i]);
    } else {
       trie_->set_base(s, 0);
    }

    // create twig for old suffix
    size_type t = trie_->create_transition(s, suffix_[start]);
    trie_->set_base(t, -(start + 1));

    // create twig for new suffix
    t = trie_->create_transition(s, *p);
    if (*p == key_type::kTerminator) {
        trie_->set_base(t, -next_suffix_);
        suffix_[next_suffix_++] = value;
    } else {
        insert_suffix(t, p + 1, value);
    }
}


void single_trie::insert(const key_type &key, const value_type &value)
{
    const char_type *p;
    size_type s = trie_->go_forward(1, key.data(), &p);
    if (trie_->base(s) < 0) {
        if (p) {
            create_branch(s, p, value);
        } else {
            // duplicated key
            suffix_[-trie_->base(s)] = value;
        }
    } else {
        s = trie_->create_transition(s, *p);
        if (*p == key_type::kTerminator) {
            trie_->set_base(s, -next_suffix_);
            suffix_[next_suffix_++] = value;
        } else {
            insert_suffix(s, p + 1, value);
        }
    }
}

bool single_trie::search(const key_type &key, value_type *value) const
{
    const char_type *p;
    size_type s = trie_->go_forward(1, key.data(), &p);
    if (trie_->base(s) < 0) {
        size_type start = -trie_->base(s);
        if (p) {
            do {
                if (*p != suffix_[start++])
                    return false;
            } while (*p++ != key_type::kTerminator);
        }
        if (value)
            *value = suffix_[start];
        return true;
    }
    return false;
}

size_t
single_trie::prefix_search(const key_type &key, result_type *result) const
{
    const char_type *p;
    size_type s = trie_->go_forward(1, key.data(), &p);
    key_type store;
    if (trie_->check_reverse_transition(s, key_type::kTerminator))
        s = trie_->prev(s);
    if (p)
        store.assign(key.data(), p - key.data());//存放key中能到达的部分
    else
        store.assign(key.data(), key.length());
    trie_->prefix_search_aux(s, p, &store, result);
    result_type::iterator it;
    for (it = result->begin(); it != result->end(); it++) {
        size_t start = -it->second;
        const char_type *miss = p;
        bool fail = false;
        if (it->first.data()[it->first.length() - 1]
            == key_type::kTerminator) {
            it->second = suffix_[start];
            continue;
        }
        for (; suffix_[start] != key_type::kTerminator; start++) {
            if (miss && *miss != key_type::kTerminator) {
                if (*miss != suffix_[start]) {
                    fail = true;
                    break;
                }
                miss++;
            }
            it->first.push(suffix_[start]);
        }
        if (fail || (miss && *miss != key_type::kTerminator)) {
            --it;
            result->erase(it + 1);
            continue;
        }
        it->second = suffix_[start + 1];
    }
    return result->size();
}

void single_trie::build(const char *filename, bool verbose)
{
    FILE *out;

    if (!filename)
        throw std::runtime_error(std::string("can not save to file ")
                                 + filename);

    if ((out = fopen(filename, "w+"))) {
        snprintf(header_->magic, sizeof(header_->magic), "%s", magic_);
        header_->suffix_size = next_suffix_;
        fwrite(header_, sizeof(header_type), 1, out);
        fwrite(suffix_, sizeof(suffix_type) * header_->suffix_size, 1, out);
        fwrite(trie_->compact_header(),
               sizeof(basic_trie::header_type), 1, out);
        fwrite(trie_->states(), sizeof(basic_trie::state_type)
                               * trie_->compact_header()->size, 1, out);

        fclose(out);
        if (verbose) {
            char buf[256];
            size_t size[2];
            size[0] = sizeof(suffix_type) * header_->suffix_size;
            size[1] = sizeof(basic_trie::state_type)
                      * trie_->compact_header()->size;

            std::cerr << "suffix = " << pretty_size(size[0], buf, sizeof(buf));
            std::cerr << ", trie = " << pretty_size(size[1], buf, sizeof(buf));
            std::cerr << ", total = "
                      << pretty_size(size[0] + size[1], buf, sizeof(buf))
                      << std::endl;
        }
    }
}

END_TRIE_NAMESPACE

// vim: ts=4 sw=4 ai et
