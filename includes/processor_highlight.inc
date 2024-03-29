<?php

/**
 * @file
 * Contains the SearchApiHighlight class.
 */

/**
 * Processor for highlighting search results.
 */
class SearchApiHighlight extends SearchApiAbstractProcessor {

  /**
   * PREG regular expression for a word boundary.
   *
   * We highlight around non-indexable or CJK characters.
   *
   * @var string
   */
  protected static $boundary;

  /**
   * PREG regular expression for splitting words.
   *
   * @var string
   */
  protected static $split;

  /**
   * {@inheritdoc}
   */
  public function __construct(SearchApiIndex $index, array $options = array()) {
    parent::__construct($index, $options);

    $cjk = '\x{1100}-\x{11FF}\x{3040}-\x{309F}\x{30A1}-\x{318E}' .
        '\x{31A0}-\x{31B7}\x{31F0}-\x{31FF}\x{3400}-\x{4DBF}\x{4E00}-\x{9FCF}' .
        '\x{A000}-\x{A48F}\x{A4D0}-\x{A4FD}\x{A960}-\x{A97F}\x{AC00}-\x{D7FF}' .
        '\x{F900}-\x{FAFF}\x{FF21}-\x{FF3A}\x{FF41}-\x{FF5A}\x{FF66}-\x{FFDC}' .
        '\x{20000}-\x{2FFFD}\x{30000}-\x{3FFFD}';
    self::$boundary = '(?:(?<=[' . PREG_CLASS_UNICODE_WORD_BOUNDARY . $cjk . '])|(?=[' . PREG_CLASS_UNICODE_WORD_BOUNDARY . $cjk . ']))';
    self::$split = '/[' . PREG_CLASS_UNICODE_WORD_BOUNDARY . ']+/iu';
  }

  /**
   * {@inheritdoc}
   */
  public function configurationForm() {
    $this->options += array(
      'prefix' => '<strong>',
      'suffix' => '</strong>',
      'excerpt' => TRUE,
      'excerpt_length' => 256,
      'highlight' => 'always',
      'highlight_partial' => FALSE,
      'exclude_fields' => array(),
    );

    $form['prefix'] = array(
      '#type' => 'textfield',
      '#title' => t('Highlighting prefix'),
      '#description' => t('Text/HTML that will be prepended to all occurrences of search keywords in highlighted text.'),
      '#default_value' => $this->options['prefix'],
    );
    $form['suffix'] = array(
      '#type' => 'textfield',
      '#title' => t('Highlighting suffix'),
      '#description' => t('Text/HTML that will be appended to all occurrences of search keywords in highlighted text.'),
      '#default_value' => $this->options['suffix'],
    );
    $form['excerpt'] = array(
      '#type' => 'checkbox',
      '#title' => t('Create excerpt'),
      '#description' => t('When enabled, an excerpt will be created for searches with keywords, containing all occurrences of keywords in a fulltext field.'),
      '#default_value' => $this->options['excerpt'],
    );
    $form['excerpt_length'] = array(
      '#title' => t('Excerpt length'),
      '#description' => t('The requested length of the excerpt, in characters.'),
      '#default_value' => $this->options['excerpt_length'],
      '#type' => 'number',
      '#min' => 1,
      '#step' => 1,
      '#size' => 4,
      '#maxlength' => 4,
      '#states' => array(
        'visible' => array(
          '#edit-processors-search-api-highlighting-settings-excerpt' => array(
            'checked' => TRUE,
          ),
        ),
      ),
    );
    // Exclude certain fulltext fields.
    $fields = $this->index->getFields();
    $fulltext_fields = array();
    foreach ($this->index->getFulltextFields() as $field) {
      if (isset($fields[$field])) {
        $fulltext_fields[$field] = check_plain($fields[$field]['name'] . ' (' . $field . ')');
      }
    }
    $form['exclude_fields'] = array(
      '#type' => 'checkboxes',
      '#title' => t('Exclude fields from excerpt'),
      '#description' => t('Exclude certain fulltext fields from being displayed in the excerpt.'),
      '#options' => $fulltext_fields,
      '#default_value' => $this->options['exclude_fields'],
      '#attributes' => array('class' => array('search-api-checkboxes-list')),
    );
    $form['highlight'] = array(
      '#type' => 'select',
      '#title' => t('Highlight returned field data'),
      '#description' => t('Select whether returned fields should be highlighted.'),
      '#options' => array(
        'always' => t('Always'),
        'server' => t('If the server returns fields'),
        'never' => t('Never'),
      ),
      '#default_value' => $this->options['highlight'],
    );

    $form['highlight_partial'] = array(
      '#type' => 'checkbox',
      '#title' => t('Highlight partial matches'),
      '#description' => t('When enabled, matches in parts of words will be highlighted as well.'),
      '#default_value' => $this->options['highlight_partial'],
    );

    return $form;
  }

  /**
   * {@inheritdoc}
   */
  public function configurationFormValidate(array $form, array &$values, array &$form_state) {
    $values['exclude_fields'] = array_filter($values['exclude_fields']);
  }

  /**
   * {@inheritdoc}
   */
  public function postprocessSearchResults(array &$response, SearchApiQuery $query) {
    if (empty($response['results']) || !($keys = $this->getKeywords($query))) {
      return;
    }

    $fulltext_fields = $this->index->getFulltextFields();
    if (!empty($this->options['exclude_fields'])) {
      $fulltext_fields = backdrop_map_assoc($fulltext_fields);
      foreach ($this->options['exclude_fields'] as $field) {
        unset($fulltext_fields[$field]);
      }
    }

    foreach ($response['results'] as $id => &$result) {
      if ($this->options['excerpt']) {
        $text = array();
        $fields = $this->getFulltextFields($response['results'], $id, $fulltext_fields);
        foreach ($fields as $data) {
          if (is_array($data)) {
            $text = array_merge($text, $data);
          }
          else {
            $text[] = $data;
          }
        }

        $result['excerpt'] = $this->createExcerpt($this->flattenArrayValues($text), $keys);
      }
      if ($this->options['highlight'] != 'never') {
        $fields = $this->getFulltextFields($response['results'], $id, $fulltext_fields, $this->options['highlight'] == 'always');
        foreach ($fields as $field => $data) {
          $result['fields'][$field] = array('#sanitize_callback' => FALSE);
          if (is_array($data)) {
            foreach ($data as $i => $text) {
              $result['fields'][$field]['#value'][$i] = $this->highlightField($text, $keys);
            }
          }
          else {
            $result['fields'][$field]['#value'] = $this->highlightField($data, $keys);
          }
        }
      }
    }
  }

  /**
   * Retrieves the fulltext data of a result.
   *
   * @param array $results
   *   All results returned in the search, by reference.
   * @param int|string $i
   *   The index in the results array of the result whose data should be
   *   returned.
   * @param array $fulltext_fields
   *   The fulltext fields from which the excerpt should be created.
   * @param bool $load
   *   TRUE if the item should be loaded if necessary, FALSE if only fields
   *   already returned in the results should be used.
   *
   * @return array
   *   An array containing fulltext field names mapped to the text data
   *   contained in them for the given result.
   */
  protected function getFulltextFields(array &$results, $i, array $fulltext_fields, $load = TRUE) {
    global $language;
    $data = array();

    $result = &$results[$i];
    // Act as if $load is TRUE if we have a loaded item.
    $load |= !empty($result['entity']);
    $result += array('fields' => array());
    // We only need detailed fields data if $load is TRUE.
    $fields = $load ? $this->index->getFields() : array();
    $needs_extraction = array();
    $returned_fields = search_api_get_sanitized_field_values(array_intersect_key($result['fields'], array_flip($fulltext_fields)));
    foreach ($fulltext_fields as $field) {
      if (array_key_exists($field, $returned_fields)) {
        $data[$field] = $returned_fields[$field];
      }
      elseif ($load) {
        $needs_extraction[$field] = $fields[$field];
      }
    }

    if (!$needs_extraction) {
      return $data;
    }

    if (empty($result['entity'])) {
      $items = $this->index->loadItems(array_keys($results));
      foreach ($items as $id => $item) {
        $results[$id]['entity'] = $item;
      }
    }
    // If we still don't have a loaded item, we should stop trying.
    if (empty($result['entity'])) {
      return $data;
    }
    $wrapper = $this->index->entityWrapper($result['entity'], FALSE);
    $wrapper->language($language->langcode);
    $extracted = search_api_extract_fields($wrapper, $needs_extraction, array('sanitize' => TRUE));

    foreach ($extracted as $field => $info) {
      if (isset($info['value'])) {
        $data[$field] = $info['value'];
      }
    }

    return $data;
  }

  /**
   * Extracts the positive keywords used in a search query.
   *
   * @param SearchApiQuery $query
   *   The query from which to extract the keywords.
   *
   * @return array
   *   An array of all unique positive keywords used in the query.
   */
  protected function getKeywords(SearchApiQuery $query) {
    $keys = $query->getKeys();
    if (!$keys) {
      return array();
    }
    if (is_array($keys)) {
      return $this->flattenKeysArray($keys);
    }

    $keywords = preg_split(self::$split, $keys);
    // Assure there are no duplicates. (This is actually faster than
    // array_unique() by a factor of 3 to 4.)
    $keywords = backdrop_map_assoc(array_filter($keywords));
    // Remove quotes from keywords.
    foreach ($keywords as $key) {
      $keywords[$key] = trim($key, "'\" ");
    }
    return backdrop_map_assoc(array_filter($keywords));
  }

  /**
   * Extracts the positive keywords from a keys array.
   *
   * @param array $keys
   *   A search keys array, as specified by SearchApiQueryInterface::getKeys().
   *
   * @return array
   *   An array of all unique positive keywords contained in the keys.
   */
  protected function flattenKeysArray(array $keys) {
    if (!empty($keys['#negation'])) {
      return array();
    }

    $keywords = array();
    foreach ($keys as $i => $key) {
      if (!element_child($i)) {
        continue;
      }
      if (is_array($key)) {
        $keywords += $this->flattenKeysArray($key);
      }
      else {
        $keywords[$key] = trim($key);
      }
    }

    return $keywords;
  }

  /**
   * Returns snippets from a piece of text, with certain keywords highlighted.
   *
   * Largely copied from search_excerpt().
   *
   * @param string $text
   *   The text to extract fragments from.
   * @param array $keys
   *   Search keywords entered by the user.
   *
   * @return string|null
   *   A string containing HTML for the excerpt, or NULL if none could be
   *   created.
   */
  protected function createExcerpt($text, array $keys) {
    // Prepare text by stripping HTML tags and decoding HTML entities.
    $text = strip_tags(str_replace(array('<', '>'), array(' <', '> '), $text));
    $text = decode_entities($text);
    $text = preg_replace('/\s+/', ' ', $text);
    $text = trim($text, ' ');
    $text_length = strlen($text);

    // Try to reach the requested excerpt length with about two fragments (each
    // with a keyword and some context).
    $ranges = array();
    $length = 0;
    $look_start = array();
    $remaining_keys = $keys;

    // Get the set excerpt length from the configuration. If the length is too
    // small, only use one fragment.
    $excerpt_length = $this->options['excerpt_length'];
    $context_length = round($excerpt_length / 4) - 3;
    if ($context_length < 32) {
      $context_length = round($excerpt_length / 2) - 1;
    }

    while ($length < $excerpt_length && !empty($remaining_keys)) {
      $found_keys = array();
      foreach ($remaining_keys as $key) {
        if ($length >= $excerpt_length) {
          break;
        }

        // Remember where we last found $key, in case we are coming through a
        // second time.
        if (!isset($look_start[$key])) {
          $look_start[$key] = 0;
        }

        // See if we can find $key after where we found it the last time. Since
        // we are requiring a match on a word boundary, make sure $text starts
        // and ends with a space.
        $matches = array();

        if (empty($this->options['highlight_partial'])) {
          $found_position = FALSE;
          $regex = '/' . static::$boundary . preg_quote($key, '/') . static::$boundary . '/iu';
          if (preg_match($regex, ' ' . $text . ' ', $matches, PREG_OFFSET_CAPTURE, $look_start[$key])) {
            $found_position = $matches[0][1];
          }
        }
        else {
          $found_position = stripos($text, $key, $look_start[$key]);
        }
        if ($found_position !== FALSE) {
          $look_start[$key] = $found_position + 1;
          // Keep track of which keys we found this time, in case we need to
          // pass through again to find more text.
          $found_keys[] = $key;

          // Locate a space before and after this match, leaving some context on
          // each end.
          if ($found_position > $context_length) {
            $before = strpos($text, ' ', $found_position - $context_length);
            if ($before !== FALSE) {
              ++$before;
            }
          }
          else {
            $before = 0;
          }
          if ($before !== FALSE && $before <= $found_position) {
            if ($text_length > $found_position + $context_length) {
              $after = strrpos(substr($text, 0, $found_position + $context_length), ' ', $found_position);
            }
            else {
              $after = $text_length;
            }
            if ($after !== FALSE && $after > $found_position) {
              if ($before < $after) {
                // Save this range.
                $ranges[$before] = $after;
                $length += $after - $before;
              }
            }
          }
        }
      }
      // Next time through this loop, only look for keys we found this time,
      // if any.
      $remaining_keys = $found_keys;
    }

    if (!$ranges) {
      // We didn't find any keyword matches, return NULL.
      return NULL;
    }

    // Sort the text ranges by starting position.
    ksort($ranges);

    // Collapse overlapping text ranges into one. The sorting makes it O(n).
    $newranges = array();
    $from1 = $to1 = NULL;
    foreach ($ranges as $from2 => $to2) {
      if ($from1 === NULL) {
        // This is the first time through this loop: initialize.
        $from1 = $from2;
        $to1 = $to2;
        continue;
      }
      if ($from2 <= $to1) {
        // The ranges overlap: combine them.
        $to1 = max($to1, $to2);
      }
      else {
        // The ranges do not overlap: save the working range and start a new
        // one.
        $newranges[$from1] = $to1;
        $from1 = $from2;
        $to1 = $to2;
      }
    }
    // Save the remaining working range.
    $newranges[$from1] = $to1;

    // Fetch text within the combined ranges we found.
    $out = array();
    foreach ($newranges as $from => $to) {
      $out[] = substr($text, $from, $to - $from);
    }
    if (!$out) {
      return NULL;
    }

    // Let translators have the ... separator text as one chunk.
    $dots = explode('!excerpt', t('... !excerpt ... !excerpt ...'));

    $text = (isset($newranges[0]) ? '' : $dots[0]) . implode($dots[1], $out) . $dots[2];
    $text = check_plain($text);

    // Since we stripped the tags at the beginning, highlighting doesn't need to
    // handle HTML anymore.
    return $this->highlightField($text, $keys, FALSE);
  }

  /**
   * Marks occurrences of the search keywords in a text field.
   *
   * @param string $text
   *   The text of the field.
   * @param array $keys
   *   Search keywords entered by the user.
   * @param bool $html
   *   Whether the text can contain HTML tags or not. In the former case, text
   *   inside tags (i.e., tag names and attributes) won't be highlighted.
   *
   * @return string
   *   The field's text with all occurrences of search keywords highlighted.
   */
  protected function highlightField($text, array $keys, $html = TRUE) {
    if (is_array($text)) {
      $text = $this->flattenArrayValues($text);
    }

    if ($html) {
      $texts = preg_split('#((?:</?[[:alpha:]](?:[^>"\']*|"[^"]*"|\'[^\']\')*>)+)#i', $text, -1, PREG_SPLIT_DELIM_CAPTURE);
      for ($i = 0; $i < count($texts); $i += 2) {
        $texts[$i] = $this->highlightField($texts[$i], $keys, FALSE);
      }
      return implode('', $texts);
    }

    $keys = implode('|', array_map('preg_quote', $keys, array_fill(0, count($keys), '/')));
    // If "Highlight partial matches" is disabled, we only want to highlight
    // matches that are complete words. Otherwise, we want all of them.
    $boundary = empty($this->options['highlight_partial']) ? self::$boundary : '';
    $regex = '/' . $boundary . '(?:' . $keys . ')' . $boundary . '/iu';
    $replace = $this->options['prefix'] . '\0' . $this->options['suffix'];
    $text = preg_replace($regex, $replace, ' ' . $text . ' ');
    return substr($text, 1, -1);
  }

  /**
   * Flattens a (possibly multidimensional) array into a string.
   *
   * @param array $array
   *   The array to flatten.
   * @param string $glue
   *   (optional) The separator to insert between individual array items.
   *
   * @return string
   *   The glued string.
   */
  protected function flattenArrayValues(array $array, $glue = " \n\n ") {
    $ret = array();
    foreach ($array as $item) {
      if (is_array($item)) {
        $ret[] = $this->flattenArrayValues($item, $glue);
      }
      else {
        $ret[] = $item;
      }
    }

    return implode($glue, $ret);
  }

}
